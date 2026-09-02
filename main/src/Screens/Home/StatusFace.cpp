#include "StatusFace.h"
#include <cstdio>
#include <esp_timer.h>
#include "Util/Services.h"
#include "Devices/Input.h"
#include "Settings/Settings.h"
#include "Services/SleepMan.h"
#include "Screens/MainMenu/MainMenu.h"
#include "Screens/NetTest/NetTestScreen.h"
#include "Screens/Home/GpuDetail.h"

StatusFace::StatusFace() : ts(*((Time*) Services.get(Service::Time))), service((StatusService*) Services.get(Service::BoxStatus)), queue(8){
	haveData = service && service->getLatest(data);
	fg = lv_color_white();
	bgColor = lv_color_black();
	warn = lv_color_make(255, 60, 60);
	if(Settings* settings = (Settings*) Services.get(Service::Settings)){
		fg = settings->get().themeData.primaryColor;
	}

	lv_obj_set_size(*this, Width, 128);

	bg = lv_obj_create(*this);
	lv_obj_set_pos(bg, 0, 0);
	lv_obj_set_size(bg, Width, 128);
	lv_obj_set_style_bg_color(bg, bgColor, 0);
	lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(bg, 0, 0);
	lv_obj_set_style_border_width(bg, 0, 0);
	lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

	statusBar = new StatusBar(bg, false);
	lv_obj_set_pos(*statusBar, 0, 0);

	// Clock line: big time on the left, host name small on the right
	clockLabel = lv_label_create(bg);
	lv_obj_set_style_text_font(clockLabel, &lv_font_unscii_16, 0);
	lv_obj_set_style_text_color(clockLabel, fg, 0);
	lv_obj_set_pos(clockLabel, 2, StatusBarH + 2);
	lv_label_set_text(clockLabel, "--:--");

	hostLabel = lv_label_create(bg);
	lv_obj_set_style_text_font(hostLabel, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(hostLabel, fg, 0);
	lv_obj_set_style_text_opa(hostLabel, LV_OPA_70, 0);
	lv_obj_set_style_text_align(hostLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_width(hostLabel, 60);
	lv_obj_set_pos(hostLabel, Width - 62, StatusBarH + 7);

	lv_coord_t y = StatusBarH + 22;
	cpuRow = makeBarRow(y, "CPU"); y += RowH;
	ramRow = makeBarRow(y, "RAM"); y += RowH;
	rootRow = makeBarRow(y, "NVME"); y += RowH;
	flashRow = makeBarRow(y, "FLSH"); y += RowH;
	binhostLabel = makeTextRow(y); y += RowH;
	alertsLabel = makeTextRow(y); y += RowH;

	footerLabel = lv_label_create(bg);
	lv_obj_set_style_text_font(footerLabel, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(footerLabel, fg, 0);
	lv_obj_set_style_text_opa(footerLabel, LV_OPA_60, 0);
	lv_obj_set_width(footerLabel, Width - 4);
	lv_obj_set_pos(footerLabel, 2, 128 - 10);

	render();
	updateClock();
	updateFooter();
}

StatusFace::~StatusFace() = default;

uint32_t StatusFace::millis(){
	return (uint32_t) (esp_timer_get_time() / 1000);
}

StatusFace::BarRow StatusFace::makeBarRow(lv_coord_t y, const char* name){
	BarRow row{};
	row.label = lv_label_create(bg);
	lv_obj_set_style_text_font(row.label, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(row.label, fg, 0);
	lv_label_set_text(row.label, name);
	lv_obj_set_pos(row.label, 2, y + 2);

	row.bar = lv_bar_create(bg);
	lv_obj_set_size(row.bar, Width - LabelW - ValueW - 6, 6);
	lv_obj_set_pos(row.bar, LabelW + 2, y + 3);
	lv_bar_set_range(row.bar, 0, 100);
	lv_obj_set_style_bg_color(row.bar, fg, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(row.bar, LV_OPA_30, LV_PART_MAIN);
	lv_obj_set_style_bg_color(row.bar, fg, LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(row.bar, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_radius(row.bar, 1, LV_PART_MAIN);
	lv_obj_set_style_radius(row.bar, 1, LV_PART_INDICATOR);
	lv_obj_set_style_pad_all(row.bar, 0, 0);

	row.value = lv_label_create(bg);
	lv_obj_set_style_text_font(row.value, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(row.value, fg, 0);
	lv_obj_set_style_text_align(row.value, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_width(row.value, ValueW);
	lv_obj_set_pos(row.value, Width - ValueW - 2, y + 2);
	return row;
}

lv_obj_t* StatusFace::makeTextRow(lv_coord_t y){
	lv_obj_t* label = lv_label_create(bg);
	lv_obj_set_style_text_font(label, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(label, fg, 0);
	lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_width(label, Width - 4);
	lv_obj_set_pos(label, 2, y + 2);
	return label;
}

void StatusFace::setBar(BarRow& row, uint8_t pct, const char* value, bool alarm){
	lv_bar_set_value(row.bar, pct, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(row.bar, alarm ? warn : fg, LV_PART_INDICATOR);
	lv_label_set_text(row.value, value);
	lv_obj_set_style_text_color(row.value, alarm ? warn : fg, 0);
}

void StatusFace::setData(const StatusData& newData){
	data = newData;
	haveData = true;
	render();
	updateFooter();
}

std::string StatusFace::ago(uint32_t s){
	char buf[16];
	if(s < 60) snprintf(buf, sizeof(buf), "%lus", (unsigned long) s);
	else if(s < 3600) snprintf(buf, sizeof(buf), "%lum", (unsigned long) (s / 60));
	else if(s < 86400) snprintf(buf, sizeof(buf), "%luh", (unsigned long) (s / 3600));
	else snprintf(buf, sizeof(buf), "%lud", (unsigned long) (s / 86400));
	return buf;
}

void StatusFace::render(){
	char buf[48];

	if(!haveData){
		lv_label_set_text(hostLabel, "");
		setBar(cpuRow, 0, "--", false);
		setBar(ramRow, 0, "--", false);
		setBar(rootRow, 0, "--", false);
		setBar(flashRow, 0, "--", false);
		lv_label_set_text(binhostLabel, "BUILD waiting for box");
		lv_obj_set_style_text_color(binhostLabel, fg, 0);
		lv_label_set_text(alertsLabel, "ALERTS --");
		lv_obj_set_style_text_color(alertsLabel, fg, 0);
		return;
	}

	lv_label_set_text(hostLabel, data.host.valid ? data.host.name.c_str() : "");

	if(data.host.valid){
		snprintf(buf, sizeof(buf), "%u%% %.1f", data.host.cpuPct, data.host.load1);
		setBar(cpuRow, data.host.cpuPct, buf, data.host.cpuPct >= 90 || data.host.cpuTempC >= 85);
		snprintf(buf, sizeof(buf), "%u%%/%.0fG", data.host.memUsedPct, data.host.memTotalGb);
		setBar(ramRow, data.host.memUsedPct, buf, data.host.memUsedPct >= 90);
		snprintf(buf, sizeof(buf), "%.0fG free", data.host.rootFreeGb);
		setBar(rootRow, data.host.rootUsedPct, buf, data.host.rootUsedPct >= 85);
		snprintf(buf, sizeof(buf), "%.0fG free", data.host.flashFreeGb);
		setBar(flashRow, data.host.flashUsedPct, buf, data.host.flashUsedPct >= 90);
	}else{
		setBar(cpuRow, 0, "n/a", false);
		setBar(ramRow, 0, "n/a", false);
		setBar(rootRow, 0, "n/a", false);
		setBar(flashRow, 0, "n/a", false);
	}

	if(!data.binhost.valid){
		lv_label_set_text(binhostLabel, "BUILD n/a");
		lv_obj_set_style_text_color(binhostLabel, fg, 0);
	}else{
		const uint32_t now = data.ts;
		const uint32_t ref = data.binhost.lastRunTs ? data.binhost.lastRunTs : now;
		const std::string age = ago(now > ref ? now - ref : 0);
		const bool bad = data.binhost.lastRc != 0 || data.binhost.failed > 0;
		if(data.binhost.running){
			snprintf(buf, sizeof(buf), "BUILD running");
		}else if(bad){
			snprintf(buf, sizeof(buf), "BUILD FAIL rc=%d %s", data.binhost.lastRc, age.c_str());
		}else{
			snprintf(buf, sizeof(buf), "BUILD ok %s ago (%u pkg)", age.c_str(), data.binhost.merged);
		}
		lv_label_set_text(binhostLabel, buf);
		lv_obj_set_style_text_color(binhostLabel, bad ? warn : fg, 0);
	}

	if(!data.alerts.valid){
		lv_label_set_text(alertsLabel, "ALERTS n/a");
		lv_obj_set_style_text_color(alertsLabel, fg, 0);
	}else if(data.alerts.firing == 0){
		snprintf(buf, sizeof(buf), "ALERTS none  %u ctr", data.containers.valid ? data.containers.running : 0);
		lv_label_set_text(alertsLabel, buf);
		lv_obj_set_style_text_color(alertsLabel, fg, 0);
	}else{
		std::string s = "ALERTS " + std::to_string(data.alerts.firing) + ":";
		for(const auto& n : data.alerts.names) s += " " + n;
		lv_label_set_text(alertsLabel, s.c_str());
		lv_obj_set_style_text_color(alertsLabel, warn, 0);
	}
}

void StatusFace::updateClock(){
	lastClock = millis();
	const tm t = ts.getTime();
	bool h24 = true;
	if(Settings* settings = (Settings*) Services.get(Service::Settings)){
		h24 = settings->get().timeFormat24h;
	}
	char buf[12];
	if(h24){
		snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
	}else{
		int h = t.tm_hour % 12;
		if(h == 0) h = 12;
		snprintf(buf, sizeof(buf), "%d:%02d%s", h, t.tm_min, t.tm_hour < 12 ? "a" : "p");
	}
	lv_label_set_text(clockLabel, buf);
}

void StatusFace::updateFooter(){
	lastFooter = millis();
	char buf[48];
	const char* net = "no svc";
	bool bad = false;
	if(service){
		switch(service->getNetState()){
			case StatusService::NetState::Off: net = "wifi off"; break;
			case StatusService::NetState::Connecting: net = "wifi..."; break;
			case StatusService::NetState::Connected: net = "wifi ok"; break;
			case StatusService::NetState::Failed: net = "wifi FAIL"; bad = true; break;
		}
	}
	if(!haveData || data.receivedMs == 0){
		snprintf(buf, sizeof(buf), "%s  no data yet", net);
	}else{
		const uint32_t age = (millis() - data.receivedMs) / 1000;
		if(age > 120) bad = true;
		if(service && service->getConsecutiveFailures() > 0){
			snprintf(buf, sizeof(buf), "%s  %s old (%lu fail)", net, ago(age).c_str(), (unsigned long) service->getConsecutiveFailures());
		}else{
			snprintf(buf, sizeof(buf), "%s  %s old  %ddBm", net, ago(age).c_str(), service ? service->getRssi() : 0);
		}
	}
	lv_label_set_text(footerLabel, buf);
	lv_obj_set_style_text_color(footerLabel, bad ? warn : fg, 0);
}

void StatusFace::onStart(){
	queue.reset();
	Events::listen(Facility::Input, &queue);
	Events::listen(Facility::Status, &queue);
	if(service){
		StatusData d;
		if(service->getLatest(d)) setData(d);
		service->requestRefresh();
	}
	if(auto sleep = (SleepMan*) Services.get(Service::Sleep)){
		sleep->enAltLock(true);
		sleep->enAutoSleep(false); // a mounted status display stays on; Alt still sleeps it
	}
}

void StatusFace::onStop(){
	Events::unlisten(&queue);
	if(auto sleep = (SleepMan*) Services.get(Service::Sleep)){
		sleep->enAltLock(false);
		sleep->enAutoSleep(true);
	}
}

void StatusFace::loop(){
	if(statusBar) statusBar->loop();

	Event evt{};
	if(queue.get(evt, 0)){
		if(evt.facility == Facility::Status){
			auto e = (StatusService::Event*) evt.data;
			if(e->action == StatusService::Event::Updated && service){
				StatusData d;
				if(service->getLatest(d)) setData(d);
			}else{
				updateFooter();
			}
		}else if(evt.facility == Facility::Input){
			auto in = (Input::Data*) evt.data;
			if(in->btn == Input::Select && in->action == Input::Data::Press){
				free(evt.data);
				transition([](){ return std::make_unique<MainMenu>(); });
				return;
			}
			if(in->btn == Input::Up && in->action == Input::Data::Press){
				free(evt.data);
				transition([](){ return std::make_unique<NetTestScreen>(); });
				return;
			}
			if(in->btn == Input::Down && in->action == Input::Data::Press){
				free(evt.data);
				transition([](){ return std::make_unique<GpuDetail>(); });
				return;
			}
		}
		free(evt.data);
	}

	const uint32_t now = millis();
	if(now - lastClock >= ClockInterval) updateClock();
	if(now - lastFooter >= FooterInterval) updateFooter();
}
