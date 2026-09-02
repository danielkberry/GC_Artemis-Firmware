#include "StatusService.h"
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "Secrets.hpp"
#include "Sleep.h"
#include "StatusFetch.h"

static const char* TAG = "StatusService";

uint32_t StatusService::millis(){
	return (uint32_t) (esp_timer_get_time() / 1000);
}

StatusService::StatusService() : Threaded("StatusSvc", 8192, 4, 0), events(8){
	esp_log_level_set(TAG, ESP_LOG_INFO);
	Events::listen(Facility::WiFi, &events);
	Events::listen(Facility::Sleep, &events);
	nextConnectAt = millis() + 1500; // let the UI come up first
	start();
}

StatusService::~StatusService(){
	stop();
	Events::unlisten(&events);
	wifi.reset();
}

bool StatusService::getLatest(StatusData& out){
	std::lock_guard<std::mutex> l(mutex);
	if(!haveLatest) return false;
	out = latest;
	return true;
}

const char* StatusService::getLastError(){
	std::lock_guard<std::mutex> l(mutex);
	return lastError.c_str();
}

int8_t StatusService::getRssi(){
	return wifi && netState == NetState::Connected ? wifi->rssi() : 0;
}

void StatusService::setNet(NetState s){
	if(netState == s) return;
	netState = s;
	Events::post(Facility::Status, Event{ Event::NetChanged });
}

void StatusService::startConnect(){
	if(!wifi) wifi = std::make_unique<WiFiHome>();
	ESP_LOGI(TAG, "joining %s", HOME_WIFI_SSID);
	connectStartedAt = millis();
	setNet(NetState::Connecting);
	wifi->connect(HOME_WIFI_SSID, HOME_WIFI_PASS);
}

void StatusService::dropWifi(){
	if(wifi) wifi->stop();
	setNet(NetState::Off);
}

void StatusService::handleEvents(){
	::Event evt{};
	while(events.get(evt, 0)){
		if(evt.facility == Facility::WiFi){
			auto e = (WiFiHome::Event*) evt.data;
			if(e->action == WiFiHome::Event::Connected){
				ESP_LOGI(TAG, "connected %s in %lu ms", wifi ? wifi->ip() : "?", (unsigned long) (millis() - connectStartedAt));
				connectBackoff = 2000;
				setNet(NetState::Connected);
				nextFetchAt = 0; // fetch right away
			}else{
				ESP_LOGW(TAG, "wifi %s, retry in %lu ms", e->action == WiFiHome::Event::Failed ? "join failed" : "dropped", (unsigned long) connectBackoff);
				setNet(NetState::Failed);
				if(wifi) wifi->stop();
				nextConnectAt = millis() + connectBackoff;
				connectBackoff = std::min<uint32_t>(connectBackoff * 2, 60000);
			}
		}else if(evt.facility == Facility::Sleep){
			auto e = (Sleep::Event*) evt.data;
			if(e->action == Sleep::Event::SleepOn){
				ESP_LOGI(TAG, "sleep: radio off");
				paused = true;
				dropWifi();
			}else{
				ESP_LOGI(TAG, "wake: reconnecting");
				paused = false;
				nextConnectAt = millis() + 200;
			}
		}
		free(evt.data);
	}
}

void StatusService::loop(){
	handleEvents();
	const uint32_t now = millis();

	if(paused){
		vTaskDelay(pdMS_TO_TICKS(200));
		return;
	}

	switch(netState){
		case NetState::Off:
		case NetState::Failed:
			if((int32_t) (now - nextConnectAt) >= 0) startConnect();
			break;
		case NetState::Connecting:
			if(now - connectStartedAt > 30000){
				ESP_LOGW(TAG, "join timed out");
				dropWifi();
				nextConnectAt = now + connectBackoff;
			}
			break;
		case NetState::Connected:
			if(refreshNow.exchange(false) || (int32_t) (now - nextFetchAt) >= 0){
				doFetch();
				nextFetchAt = millis() + FetchInterval;
			}
			break;
	}

	vTaskDelay(pdMS_TO_TICKS(100));
}

void StatusService::doFetch(){
	if(!clockSynced){
		if(StatusFetch::clockIsSet() || StatusFetch::syncClock()){
			clockSynced = true;
		}else{
			ESP_LOGW(TAG, "clock not set; TLS will fail until SNTP works");
		}
	}

	auto r = StatusFetch::fetch();
	lastFetchMs = r.ms;
	if(!r.ok){
		failures++;
		{
			std::lock_guard<std::mutex> l(mutex);
			lastError = r.error;
		}
		ESP_LOGW(TAG, "fetch failed (%lu in a row): %s", (unsigned long) failures.load(), r.error.c_str());
		Events::post(Facility::Status, Event{ Event::FetchFailed });
		return;
	}

	StatusData d;
	if(!parse(r.body, d)){
		failures++;
		{
			std::lock_guard<std::mutex> l(mutex);
			lastError = "bad json";
		}
		ESP_LOGW(TAG, "unparseable body (%u bytes)", (unsigned) r.body.size());
		Events::post(Facility::Status, Event{ Event::FetchFailed });
		return;
	}
	d.receivedMs = millis();
	{
		std::lock_guard<std::mutex> l(mutex);
		latest = d;
		haveLatest = true;
		lastError.clear();
	}
	failures = 0;
	ESP_LOGI(TAG, "updated: gpu %u%% %dC, %u model(s), build rc=%d, %u alerts, %lu ms",
			 d.gpu.utilPct, d.gpu.tempC, (unsigned) d.ollama.models.size(), d.binhost.lastRc, d.alerts.firing, (unsigned long) r.ms);
	Events::post(Facility::Status, Event{ Event::Updated });
}

// ───────────────────────────── parsing ─────────────────────────────────────

namespace {
double num(cJSON* obj, const char* key, double def = 0){
	cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsNumber(v) ? v->valuedouble : def;
}
std::string str(cJSON* obj, const char* key){
	cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
bool boolean(cJSON* obj, const char* key){
	cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsTrue(v);
}
cJSON* block(cJSON* root, const char* key){
	cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
	return cJSON_IsObject(v) ? v : nullptr;
}
}

bool StatusService::parse(const std::string& body, StatusData& d){
	cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
	if(root == nullptr || !cJSON_IsObject(root)){
		cJSON_Delete(root);
		return false;
	}
	d = StatusData{};
	d.ts = (uint32_t) num(root, "ts");
	if(d.ts == 0){
		cJSON_Delete(root);
		return false;
	}

	if(cJSON* h = block(root, "host")){
		d.host.valid = true;
		d.host.name = str(h, "name");
		d.host.uptimeS = (uint32_t) num(h, "uptime_s");
		d.host.load1 = (float) num(h, "load1");
		d.host.memUsedPct = (uint8_t) num(h, "mem_used_pct");
		d.host.cpuTempC = (int16_t) num(h, "cpu_temp_c");
		d.host.rootFreeGb = (float) num(h, "root_free_gb");
		d.host.flashFreeGb = (float) num(h, "flash_free_gb");
	}
	if(cJSON* g = block(root, "gpu")){
		d.gpu.valid = true;
		d.gpu.name = str(g, "name");
		d.gpu.utilPct = (uint8_t) num(g, "util_pct");
		d.gpu.tempC = (int16_t) num(g, "temp_c");
		d.gpu.vramUsedMb = (uint16_t) num(g, "vram_used_mb");
		d.gpu.vramTotalMb = (uint16_t) num(g, "vram_total_mb");
		d.gpu.powerW = (float) num(g, "power_w");
		d.gpu.fanPct = (uint8_t) num(g, "fan_pct");
	}
	if(cJSON* o = block(root, "ollama")){
		d.ollama.valid = true;
		cJSON* models = cJSON_GetObjectItemCaseSensitive(o, "models");
		cJSON* m;
		cJSON_ArrayForEach(m, models){
			if(cJSON_IsObject(m)){
				const std::string n = str(m, "name");
				if(!n.empty()) d.ollama.models.push_back(n);
			}
		}
	}
	if(cJSON* b = block(root, "binhost")){
		d.binhost.valid = true;
		d.binhost.lastRc = (int16_t) num(b, "last_rc", -1);
		d.binhost.lastRunTs = (uint32_t) num(b, "last_run_ts");
		d.binhost.lastSuccessTs = (uint32_t) num(b, "last_success_ts");
		d.binhost.running = boolean(b, "running");
		d.binhost.merged = (uint16_t) num(b, "merged");
		d.binhost.failed = (uint16_t) num(b, "failed");
	}
	if(cJSON* a = block(root, "alerts")){
		d.alerts.valid = true;
		d.alerts.firing = (uint8_t) num(a, "firing");
		cJSON* names = cJSON_GetObjectItemCaseSensitive(a, "names");
		cJSON* n;
		cJSON_ArrayForEach(n, names){
			if(cJSON_IsString(n) && n->valuestring) d.alerts.names.push_back(n->valuestring);
		}
	}
	if(cJSON* c = block(root, "containers")){
		d.containers.valid = true;
		d.containers.running = (uint16_t) num(c, "running");
		d.containers.unhealthy = (uint16_t) num(c, "unhealthy");
	}
	cJSON* errs = cJSON_GetObjectItemCaseSensitive(root, "errors");
	cJSON* e;
	cJSON_ArrayForEach(e, errs){
		if(cJSON_IsString(e) && e->valuestring) d.errors.push_back(e->valuestring);
	}

	cJSON_Delete(root);
	return true;
}
