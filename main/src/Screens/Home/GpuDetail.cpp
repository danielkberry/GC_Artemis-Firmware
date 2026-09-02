#include "GpuDetail.h"
#include <cstdio>
#include "Util/Services.h"
#include "Devices/Input.h"
#include "Settings/Settings.h"
#include "Services/SleepMan.h"
#include "Screens/Home.h"

GpuDetail::GpuDetail() : service((StatusService*) Services.get(Service::BoxStatus)), queue(6){
	fg = lv_color_white();
	warn = lv_color_make(255, 60, 60);
	if(Settings* settings = (Settings*) Services.get(Service::Settings)){
		fg = settings->get().themeData.primaryColor;
	}

	bg = lv_obj_create(*this);
	lv_obj_set_pos(bg, 0, 0);
	lv_obj_set_size(bg, 128, 128);
	lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(bg, 0, 0);
	lv_obj_set_style_border_width(bg, 0, 0);
	lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

	for(uint8_t i = 0; i < Rows; i++){
		rows[i] = lv_label_create(bg);
		lv_obj_set_style_text_font(rows[i], &lv_font_unscii_8, 0);
		lv_obj_set_style_text_color(rows[i], fg, 0);
		lv_obj_set_width(rows[i], 124);
		lv_label_set_long_mode(rows[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
		lv_obj_set_pos(rows[i], 2, 3 + i * 13);
		lv_label_set_text(rows[i], "");
	}

	auto makeBar = [&](lv_coord_t y){
		lv_obj_t* bar = lv_bar_create(bg);
		lv_obj_set_size(bar, 124, 4);
		lv_obj_set_pos(bar, 2, y);
		lv_bar_set_range(bar, 0, 100);
		lv_obj_set_style_bg_color(bar, fg, LV_PART_MAIN);
		lv_obj_set_style_bg_opa(bar, LV_OPA_30, LV_PART_MAIN);
		lv_obj_set_style_bg_color(bar, fg, LV_PART_INDICATOR);
		lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
		lv_obj_set_style_radius(bar, 1, LV_PART_MAIN);
		lv_obj_set_style_radius(bar, 1, LV_PART_INDICATOR);
		lv_obj_set_style_pad_all(bar, 0, 0);
		return bar;
	};
	utilBar = makeBar(3 + 2 * 13 - 3);
	vramBar = makeBar(3 + 4 * 13 - 3);

	render();
}

void GpuDetail::render(){
	StatusData d;
	if(!service || !service->getLatest(d)){
		lv_label_set_text(rows[0], "GPU  (no data yet)");
		return;
	}
	char buf[48];
	const auto& g = d.gpu;
	if(!g.valid){
		lv_label_set_text(rows[0], "GPU  n/a");
		return;
	}
	snprintf(buf, sizeof(buf), "GPU %s", g.name.c_str());
	lv_label_set_text(rows[0], buf);
	snprintf(buf, sizeof(buf), "util %u%%   %dC", g.utilPct, g.tempC);
	lv_label_set_text(rows[1], buf);
	lv_obj_set_style_text_color(rows[1], g.tempC >= 80 ? warn : fg, 0);
	lv_bar_set_value(utilBar, g.utilPct, LV_ANIM_OFF);
	const uint8_t vramPct = g.vramTotalMb ? (uint8_t) ((100u * g.vramUsedMb) / g.vramTotalMb) : 0;
	snprintf(buf, sizeof(buf), "vram %u/%u MB (%u%%)", g.vramUsedMb, g.vramTotalMb, vramPct);
	lv_label_set_text(rows[3], buf);
	lv_bar_set_value(vramBar, vramPct, LV_ANIM_OFF);
	snprintf(buf, sizeof(buf), "power %.1f W  fan %u%%", g.powerW, g.fanPct);
	lv_label_set_text(rows[5], buf);
	if(!d.ollama.valid){
		lv_label_set_text(rows[6], "model n/a");
	}else if(d.ollama.models.empty()){
		lv_label_set_text(rows[6], "model idle");
	}else{
		snprintf(buf, sizeof(buf), "model %s", d.ollama.models[0].c_str());
		lv_label_set_text(rows[6], buf);
	}
	snprintf(buf, sizeof(buf), "cpu %dC  load %.2f/%u", d.host.cpuTempC, d.host.load1, d.host.cores);
	lv_label_set_text(rows[7], buf);
	snprintf(buf, sizeof(buf), "up %lud %luh  %u ctr", (unsigned long) (d.host.uptimeS / 86400), (unsigned long) ((d.host.uptimeS % 86400) / 3600),
			 d.containers.valid ? d.containers.running : 0);
	lv_label_set_text(rows[8], buf);
}

void GpuDetail::onStart(){
	queue.reset();
	Events::listen(Facility::Input, &queue);
	Events::listen(Facility::Status, &queue);
	if(auto sleep = (SleepMan*) Services.get(Service::Sleep)) sleep->enAutoSleep(false);
}

void GpuDetail::onStop(){
	Events::unlisten(&queue);
	if(auto sleep = (SleepMan*) Services.get(Service::Sleep)) sleep->enAutoSleep(true);
}

void GpuDetail::loop(){
	Event evt{};
	if(queue.get(evt, 0)){
		if(evt.facility == Facility::Status){
			render();
		}else if(evt.facility == Facility::Input){
			auto in = (Input::Data*) evt.data;
			if(in->action == Input::Data::Press && (in->btn == Input::Alt || in->btn == Input::Up)){
				free(evt.data);
				transition([](){ return makeHomeScreen(); });
				return;
			}
		}
		free(evt.data);
	}
}
