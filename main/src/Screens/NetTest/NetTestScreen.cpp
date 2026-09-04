#include "NetTestScreen.h"
#include <cstdio>
#include <esp_log.h>
#include <esp_timer.h>
#include "Secrets.hpp"
#include "Devices/Input.h"
#include "Services/SleepMan.h"
#include "Services/StatusFetch.h"
#include "Services/StatusService.h"
#include "Screens/Home.h"
#include "Util/Services.h"

static const char* TAG = "NetTest";

static uint32_t nowMs(){
	return (uint32_t) (esp_timer_get_time() / 1000);
}

NetTestScreen::NetTestScreen() : worker([this](){ workerFunc(); }, "nettest", 8192, 4, 0), queue(6){
	// The firmware's default log level is WARN; the measurements below are INFO.
	esp_log_level_set("NetTest", ESP_LOG_INFO);
	esp_log_level_set("WiFiHome", ESP_LOG_INFO);
	esp_log_level_set("StatusFetch", ESP_LOG_INFO);

	bg = lv_obj_create(*this);
	lv_obj_set_pos(bg, 0, 0);
	lv_obj_set_size(bg, 128, 128);
	lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(bg, 0, 0);
	lv_obj_set_style_border_width(bg, 0, 0);
	lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

	for(uint8_t i = 0; i < MaxLines; i++){
		lines[i] = lv_label_create(bg);
		lv_obj_set_style_text_font(lines[i], &lv_font_unscii_8, 0);
		lv_obj_set_style_text_color(lines[i], lv_color_white(), 0);
		lv_obj_set_width(lines[i], 126);
		lv_label_set_long_mode(lines[i], LV_LABEL_LONG_CLIP);
		lv_obj_set_pos(lines[i], 1, 2 + i * 11);
		lv_label_set_text(lines[i], "");
	}
	say("NET  Alt:home Dn:buzz");
}

NetTestScreen::~NetTestScreen() = default;

void NetTestScreen::say(const std::string& s){
	ESP_LOGI(TAG, "%s", s.c_str());
	std::lock_guard<std::mutex> l(logMutex);
	log.push_back(s);
	if(log.size() > MaxLines) log.erase(log.begin() + 1); // keep the title line
	dirty = true;
}

void NetTestScreen::repaint(){
	std::lock_guard<std::mutex> l(logMutex);
	if(!dirty) return;
	dirty = false;
	for(uint8_t i = 0; i < MaxLines; i++){
		lv_label_set_text(lines[i], i < log.size() ? log[i].c_str() : "");
	}
}

void NetTestScreen::onStart(){
	queue.reset();
	Events::listen(Facility::Input, &queue);
	if(auto sleep = (SleepMan*) Services.get(Service::Sleep)) sleep->enAutoSleep(false);
	worker.start();
}

void NetTestScreen::onStop(){
	runRequested = false;
	if(worker.running()) worker.stop();
	Events::unlisten(&queue);
	wifi.reset();
	if(auto sleep = (SleepMan*) Services.get(Service::Sleep)) sleep->enAutoSleep(true);
}

void NetTestScreen::goHome(){
	transition([](){ return makeHomeScreen(); });
}

void NetTestScreen::loop(){
	repaint();

	Event evt{};
	if(queue.get(evt, 0)){
		if(evt.facility == Facility::Input){
			auto in = (Input::Data*) evt.data;
			if(in->action == Input::Data::Press){
				if(in->btn == Input::Alt){
					free(evt.data);
					goHome();
					return;
				}else if(in->btn == Input::Select && workerDone){
					runRequested = true;
				}else if(in->btn == Input::Down){
					if(auto svc = (StatusService*) Services.get(Service::BoxStatus)){
						say("test: alert buzz");
						svc->alertFeedback();
					}
				}
			}
		}
		free(evt.data);
	}
}

void NetTestScreen::workerFunc(){
	if(runRequested.exchange(false)){
		workerDone = false;
		runSequence();
		workerDone = true;
	}
	vTaskDelay(pdMS_TO_TICKS(100));
}

void NetTestScreen::runSequence(){
	char buf[64];
	auto h = StatusFetch::heap();

	// When StatusService owns the radio, this screen is a diagnostics view of it
	// (a second esp_wifi_init would abort). Select re-reads; the service refreshes.
	if(auto svc = (StatusService*) Services.get(Service::BoxStatus)){
		static const char* NetNames[] = { "off", "joining", "ok", "FAIL" };
		svc->requestRefresh();
		vTaskDelay(pdMS_TO_TICKS(2500));
		snprintf(buf, sizeof(buf), "net %s %ddB", NetNames[(int) svc->getNetState()], svc->getRssi());
		say(buf);
		snprintf(buf, sizeof(buf), "fetch %lums x%lu", (unsigned long) svc->getLastFetchMs(), (unsigned long) svc->getConsecutiveFailures());
		say(buf);
		if(svc->getLastError()[0]) say(std::string("err ") + svc->getLastError());
		StatusData d;
		if(svc->getLatest(d)){
			snprintf(buf, sizeof(buf), "data %lus old", (unsigned long) ((nowMs() - d.receivedMs) / 1000));
			say(buf);
			snprintf(buf, sizeof(buf), "gpu %u%% %dC %uM", d.gpu.utilPct, d.gpu.tempC, d.gpu.vramUsedMb);
			say(buf);
		}else{
			say("no data yet");
		}
		snprintf(buf, sizeof(buf), "heap %luk min %luk", (unsigned long) h.internal / 1024, (unsigned long) h.minEverInternal / 1024);
		say(buf);
		return;
	}

	snprintf(buf, sizeof(buf), "heap0 int %luk lrg %luk", (unsigned long) h.internal / 1024, (unsigned long) h.largestInternal / 1024);
	say(buf);

	const uint32_t t0 = nowMs();
	if(!wifi) wifi = std::make_unique<WiFiHome>();
	h = StatusFetch::heap();
	snprintf(buf, sizeof(buf), "wifi init %lums int %luk", (unsigned long) (nowMs() - t0), (unsigned long) h.internal / 1024);
	say(buf);

	say(std::string("join ") + HOME_WIFI_SSID);
	wifi->connect(HOME_WIFI_SSID, HOME_WIFI_PASS);
	const uint32_t deadline = nowMs() + 15000;
	while(wifi->getState() == WiFiHome::State::Connecting && nowMs() < deadline){
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	if(wifi->getState() != WiFiHome::State::Connected){
		say("join FAILED");
		h = StatusFetch::heap();
		snprintf(buf, sizeof(buf), "heap int %luk min %luk", (unsigned long) h.internal / 1024, (unsigned long) h.minEverInternal / 1024);
		say(buf);
		wifi->stop();
		return;
	}
	snprintf(buf, sizeof(buf), "ip %s rssi %d", wifi->ip(), wifi->rssi());
	say(buf);
	snprintf(buf, sizeof(buf), "assoc %lums ip %lums", (unsigned long) wifi->msToAssoc, (unsigned long) wifi->msToIp);
	say(buf);

	if(!StatusFetch::clockIsSet()){
		const uint32_t took = StatusFetch::syncClock();
		snprintf(buf, sizeof(buf), took ? "sntp %lums" : "sntp FAILED", (unsigned long) took);
		say(buf);
	}else{
		say("clock already set");
	}

	auto r = StatusFetch::fetch();
	if(r.ok){
		snprintf(buf, sizeof(buf), "https 200 %uB %lums", (unsigned) r.body.size(), (unsigned long) r.ms);
	}else{
		snprintf(buf, sizeof(buf), "https FAIL %s %lums", r.error.c_str(), (unsigned long) r.ms);
	}
	say(buf);
	if(r.ok) say(r.body.substr(0, 40));
	h = StatusFetch::heap();
	snprintf(buf, sizeof(buf), "tls min int %luk", (unsigned long) r.minFreeInternal / 1024);
	say(buf);
	snprintf(buf, sizeof(buf), "heap int %luk min %luk", (unsigned long) h.internal / 1024, (unsigned long) h.minEverInternal / 1024);
	say(buf);

	// Second fetch on the same association: the steady-state cost once WiFi is up
	auto r2 = StatusFetch::fetch();
	snprintf(buf, sizeof(buf), "fetch#2 %s %lums", r2.ok ? "ok" : "FAIL", (unsigned long) r2.ms);
	say(buf);
	snprintf(buf, sizeof(buf), "total %lums", (unsigned long) (nowMs() - t0));
	say(buf);
}
