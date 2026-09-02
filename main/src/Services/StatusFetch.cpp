#include "StatusFetch.h"
#include <ctime>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include "Secrets.hpp"
#include "Util/Services.h"
#include "Time.h"

static const char* TAG = "StatusFetch";

static uint32_t nowMs(){
	return (uint32_t) (esp_timer_get_time() / 1000);
}

StatusFetch::Heap StatusFetch::heap(){
	return {
			(uint32_t) esp_get_free_heap_size(),
			(uint32_t) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
			(uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
			(uint32_t) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
	};
}

bool StatusFetch::clockIsSet(){
	return time(nullptr) > 1704067200; // 2024-01-01
}

uint32_t StatusFetch::syncClock(uint32_t timeoutMs){
	const uint32_t t0 = nowMs();
	esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
	cfg.start = true;
	cfg.server_from_dhcp = false;
	esp_err_t err = esp_netif_sntp_init(&cfg);
	if(err != ESP_OK){
		ESP_LOGE(TAG, "sntp init: %s", esp_err_to_name(err));
		return 0;
	}
	err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeoutMs));
	esp_netif_sntp_deinit();
	if(err != ESP_OK){
		ESP_LOGE(TAG, "sntp sync: %s", esp_err_to_name(err));
		return 0;
	}
	const time_t now = time(nullptr);
	if(auto ts = (Time*) Services.get(Service::Time)){
		ts->setTime(now);
	}
	const uint32_t took = nowMs() - t0;
	ESP_LOGI(TAG, "sntp ok: %lld after %lu ms", (long long) now, (unsigned long) took);
	return took ? took : 1;
}

namespace {
struct Ctx {
	std::string* body;
	uint32_t minInternal;
	static constexpr size_t MaxBody = 8192;
};

esp_err_t onEvent(esp_http_client_event_t* evt){
	auto ctx = (Ctx*) evt->user_data;
	if(evt->event_id == HTTP_EVENT_ON_DATA){
		if(ctx->body->size() + evt->data_len <= Ctx::MaxBody){
			ctx->body->append((const char*) evt->data, evt->data_len);
		}
		const uint32_t fi = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		if(fi < ctx->minInternal) ctx->minInternal = fi;
	}else if(evt->event_id == HTTP_EVENT_ON_CONNECTED){
		const uint32_t fi = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		if(fi < ctx->minInternal) ctx->minInternal = fi;
	}
	return ESP_OK;
}
}

StatusFetch::Result StatusFetch::fetch(uint32_t timeoutMs){
	Result r;
	Ctx ctx{ &r.body, UINT32_MAX };
	r.body.reserve(1024);

	esp_http_client_config_t cfg = {};
	cfg.url = STATUS_URL;
	cfg.crt_bundle_attach = esp_crt_bundle_attach;
	cfg.timeout_ms = (int) timeoutMs;
	cfg.buffer_size = 2048;
	cfg.buffer_size_tx = 1024;
	cfg.event_handler = onEvent;
	cfg.user_data = &ctx;
	cfg.keep_alive_enable = false;

	const uint32_t t0 = nowMs();
	esp_http_client_handle_t client = esp_http_client_init(&cfg);
	if(client == nullptr){
		r.error = "client init";
		return r;
	}
	esp_http_client_set_header(client, "Accept", "application/json");
	const esp_err_t err = esp_http_client_perform(client);
	r.ms = nowMs() - t0;
	if(err == ESP_OK){
		r.httpStatus = esp_http_client_get_status_code(client);
		r.ok = r.httpStatus == 200 && !r.body.empty();
		if(!r.ok) r.error = "http " + std::to_string(r.httpStatus);
	}else{
		r.error = esp_err_to_name(err);
	}
	esp_http_client_cleanup(client);
	r.minFreeInternal = ctx.minInternal == UINT32_MAX ? 0 : ctx.minInternal;
	ESP_LOGI(TAG, "fetch %s: http=%d bytes=%u in %lu ms (min internal heap %lu) %s",
			 r.ok ? "ok" : "FAIL", r.httpStatus, (unsigned) r.body.size(), (unsigned long) r.ms,
			 (unsigned long) r.minFreeInternal, r.error.c_str());
	return r;
}
