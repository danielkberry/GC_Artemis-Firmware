#include "WiFiHome.h"
#include <cstring>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include "Util/Events.h"

static const char* TAG = "WiFiHome";

uint32_t WiFiHome::millis(){
	return (uint32_t) (esp_timer_get_time() / 1000);
}

WiFiHome::WiFiHome(){
	// The default event loop and esp_netif may already exist (rover control creates them too).
	esp_err_t err = esp_event_loop_create_default();
	if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
	err = esp_netif_init();
	if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);

	netif = esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, [](void* arg, esp_event_base_t, int32_t id, void* data){
		static_cast<WiFiHome*>(arg)->onWifiEvent(id, data);
	}, this, &wifiHandler));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, [](void* arg, esp_event_base_t, int32_t id, void* data){
		static_cast<WiFiHome*>(arg)->onIpEvent(id, data);
	}, this, &ipHandler));
}

WiFiHome::~WiFiHome(){
	stop();
	esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiHandler);
	esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ipHandler);
	esp_wifi_deinit();
	esp_wifi_clear_default_wifi_driver_and_handlers(netif);
	esp_netif_destroy(netif);
	netif = nullptr;
}

void WiFiHome::connect(const char* ssid, const char* pass){
	if(state == State::Connecting || state == State::Connected) return;

	wifi_config_t cfg = {};
	strncpy((char*) cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
	strncpy((char*) cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
	cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;   // mesh: several APs share the SSID
	cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
	cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	cfg.sta.pmf_cfg.capable = true;
	cfg.sta.pmf_cfg.required = false;
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

	state = State::Connecting;
	retries = 0;
	msToAssoc = msToIp = 0;
	ipStr[0] = 0;
	connectStart = millis();
	ESP_ERROR_CHECK(esp_wifi_start());   // STA_START event triggers esp_wifi_connect()
}

void WiFiHome::stop(){
	if(state == State::Idle) return;
	state = State::Idle;
	esp_wifi_disconnect();
	esp_wifi_stop();
	ipStr[0] = 0;
}

int8_t WiFiHome::rssi() const{
	if(state != State::Connected) return 0;
	wifi_ap_record_t ap{};
	if(esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
	return ap.rssi;
}

void WiFiHome::onWifiEvent(int32_t id, void* data){
	if(id == WIFI_EVENT_STA_START){
		if(state == State::Connecting) esp_wifi_connect();
	}else if(id == WIFI_EVENT_STA_CONNECTED){
		auto e = (wifi_event_sta_connected_t*) data;
		msToAssoc = millis() - connectStart;
		ESP_LOGI(TAG, "associated ch=%d after %lu ms", e->channel, (unsigned long) msToAssoc);
	}else if(id == WIFI_EVENT_STA_DISCONNECTED){
		auto e = (wifi_event_sta_disconnected_t*) data;
		ESP_LOGW(TAG, "disconnected reason=%d (state %d)", e->reason, (int) state);
		if(state == State::Connecting){
			if(++retries <= MaxRetries){
				esp_wifi_connect();
			}else{
				state = State::Failed;
				Events::post(Facility::WiFi, Event{ Event::Failed });
			}
		}else if(state == State::Connected){
			state = State::Failed;
			Events::post(Facility::WiFi, Event{ Event::Disconnected });
		}
	}
}

void WiFiHome::onIpEvent(int32_t id, void* data){
	if(id != IP_EVENT_STA_GOT_IP) return;
	auto e = (ip_event_got_ip_t*) data;
	snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&e->ip_info.ip));
	msToIp = millis() - connectStart;
	state = State::Connected;
	ESP_LOGI(TAG, "ip %s after %lu ms", ipStr, (unsigned long) msToIp);
	Events::post(Facility::WiFi, Event{ Event::Connected });
}
