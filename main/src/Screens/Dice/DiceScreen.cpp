#include "DiceScreen.h"
#include <cmath>
#include <esp_random.h>
#include <esp_timer.h>
#include "Util/Services.h"
#include "Devices/Input.h"
#include "Settings/Settings.h"
#include "Screens/MainMenu/MainMenu.h"

DiceScreen::DiceScreen() : imu((IMU*) Services.get(Service::IMU)), audio((ChirpSystem*) Services.get(Service::Audio)), queue(8),
						   reader([this](){ readerFunc(); }, "diceShake", 3072, 5, 1){
	dieColor = lv_color_white();
	pipColor = lv_color_black();
	if(Settings* settings = (Settings*) Services.get(Service::Settings)){
		dieColor = settings->get().themeData.primaryColor;
		pipColor = settings->get().themeData.secondaryColor;
	}

	bg = lv_obj_create(*this);
	lv_obj_set_pos(bg, 0, 0);
	lv_obj_set_size(bg, 128, 128);
	lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(bg, 0, 0);
	lv_obj_set_style_border_width(bg, 0, 0);
	lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

	countLabel = lv_label_create(bg);
	lv_obj_set_style_text_font(countLabel, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(countLabel, dieColor, 0);
	lv_obj_align(countLabel, LV_ALIGN_TOP_MID, 0, 4);

	for(auto& die : dice){
		die.body = lv_obj_create(bg);
		lv_obj_set_style_bg_color(die.body, dieColor, 0);
		lv_obj_set_style_bg_opa(die.body, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(die.body, 0, 0);
		lv_obj_set_style_pad_all(die.body, 0, 0);
		lv_obj_clear_flag(die.body, LV_OBJ_FLAG_SCROLLABLE);

		for(auto& pip : die.pips){
			pip = lv_obj_create(die.body);
			lv_obj_set_style_bg_color(pip, pipColor, 0);
			lv_obj_set_style_bg_opa(pip, LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(pip, 0, 0);
			lv_obj_set_style_pad_all(pip, 0, 0);
			lv_obj_set_style_radius(pip, LV_RADIUS_CIRCLE, 0);
			lv_obj_clear_flag(pip, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_add_flag(pip, LV_OBJ_FLAG_HIDDEN);
		}
	}

	totalLabel = lv_label_create(bg);
	lv_obj_set_style_text_font(totalLabel, &lv_font_unscii_16, 0);
	lv_obj_set_style_text_color(totalLabel, dieColor, 0);
	lv_obj_align(totalLabel, LV_ALIGN_BOTTOM_MID, 0, -16);

	hintLabel = lv_label_create(bg);
	lv_obj_set_style_text_font(hintLabel, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(hintLabel, dieColor, 0);
	lv_obj_set_style_text_opa(hintLabel, LV_OPA_60, 0);
	lv_label_set_text(hintLabel, "SEL/shake: roll");
	lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -3);

	for(auto& die : dice){
		die.value = 1 + esp_random() % 6;
	}
	layoutDice();
	updateLabels();
}

DiceScreen::~DiceScreen() = default;

uint32_t DiceScreen::millis(){
	return (uint32_t) (esp_timer_get_time() / 1000);
}

void DiceScreen::onStart(){
	Events::listen(Facility::Input, &queue);
	hits = 0;
	stillCount = 0;
	armed = true;
	shakeFlag = false;
	if(imu){
		reader.start();
	}
}

void DiceScreen::onStop(){
	if(reader.running()){
		reader.stop();
	}
	Events::unlisten(&queue);
}

void DiceScreen::readerFunc(){
	const IMU::Sample s = imu->getSample();
	const double mag = std::sqrt(s.accelX * s.accelX + s.accelY * s.accelY + s.accelZ * s.accelZ);
	const double dev = std::abs(mag - Gravity);
	const uint32_t now = millis();

	if(!armed){
		// Wait for the watch to settle before accepting another shake
		if(dev < HitThreshold){
			if(++stillCount >= StillSamples){
				stillCount = 0;
				hits = 0;
				armed = true;
			}
		}else{
			stillCount = 0;
		}
		vTaskDelay(ReaderDelay);
		return;
	}

	if(now - windowStart > HitWindow){
		hits = 0;
		windowStart = now;
	}

	if(dev >= HitThreshold){
		if(hits == 0){
			windowStart = now;
		}
		hits++;
		if(hits >= HitsNeeded){
			hits = 0;
			armed = false;
			shakeFlag = true;
		}
	}

	vTaskDelay(ReaderDelay);
}

void DiceScreen::loop(){
	Event evt{};
	if(queue.get(evt, 0)){
		if(evt.facility == Facility::Input){
			auto data = (Input::Data*) evt.data;
			const bool leaving = data->btn == Input::Alt && data->action == Input::Data::Press;
			handleInput(*data);
			free(evt.data);
			if(leaving) return;
		}else{
			free(evt.data);
		}
	}

	const uint32_t now = millis();

	if(rolling){
		if(now - rollStart >= RollDuration){
			finishRoll();
		}else if(now - lastTick >= RollTickPeriod){
			lastTick = now;
			randomizeFaces();
			if(audio){
				audio->play({ Chirp{ 300, 500, 15 } });
			}
		}
		return;
	}

	if(shakeFlag.exchange(false) && now - rollEnd >= ReadHold){
		startRoll();
	}
}

void DiceScreen::handleInput(const Input::Data& evt){
	if(evt.action != Input::Data::Press) return;

	switch(evt.btn){
		case Input::Alt:
			transition([](){ return std::make_unique<MainMenu>(); });
			return;
		case Input::Select:
			if(!rolling) startRoll();
			return;
		case Input::Up:
			if(rolling || diceCount >= MaxDice) return;
			diceCount++;
			break;
		case Input::Down:
			if(rolling || diceCount <= 1) return;
			diceCount--;
			break;
		default:
			return;
	}

	layoutDice();
	updateLabels();
}

void DiceScreen::layoutDice(){
	// Die size and gap chosen so 1-3 dice fit in the 128px width.
	static constexpr lv_coord_t Sizes[MaxDice] = { 56, 44, 36 };
	static constexpr lv_coord_t Gap = 6;

	const lv_coord_t size = Sizes[diceCount - 1];
	const lv_coord_t totalW = diceCount * size + (diceCount - 1) * Gap;
	const lv_coord_t x0 = (128 - totalW) / 2;
	const lv_coord_t y = 22 + (56 - size) / 2;

	const lv_coord_t pipD = std::max<lv_coord_t>(5, size / 6);
	const lv_coord_t m = size / 5;
	const lv_coord_t c = size / 2;
	const lv_coord_t f = size - m;
	// Order: TL, TR, ML, MR, BL, BR, C
	const lv_coord_t px[PipsPerDie] = { m, f, m, f, m, f, c };
	const lv_coord_t py[PipsPerDie] = { m, m, c, c, f, f, c };

	for(uint8_t i = 0; i < MaxDice; i++){
		Die& die = dice[i];
		if(i >= diceCount){
			lv_obj_add_flag(die.body, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(die.body, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_size(die.body, size, size);
		lv_obj_set_pos(die.body, x0 + i * (size + Gap), y);
		lv_obj_set_style_radius(die.body, size / 6, 0);

		for(uint8_t p = 0; p < PipsPerDie; p++){
			lv_obj_set_size(die.pips[p], pipD, pipD);
			lv_obj_set_pos(die.pips[p], px[p] - pipD / 2, py[p] - pipD / 2);
		}
		setFace(die, die.value);
	}
}

void DiceScreen::setFace(Die& die, uint8_t value){
	// Pip index bitmasks, order: TL, TR, ML, MR, BL, BR, C
	static constexpr uint8_t Faces[7] = {
			0,
			0b1000000,
			0b0100001,
			0b1100001,
			0b0110011,
			0b1110011,
			0b0111111
	};
	die.value = value;
	const uint8_t mask = Faces[value];
	for(uint8_t p = 0; p < PipsPerDie; p++){
		if(mask & (1 << p)){
			lv_obj_clear_flag(die.pips[p], LV_OBJ_FLAG_HIDDEN);
		}else{
			lv_obj_add_flag(die.pips[p], LV_OBJ_FLAG_HIDDEN);
		}
	}
}

void DiceScreen::randomizeFaces(){
	for(uint8_t i = 0; i < diceCount; i++){
		uint8_t v;
		do {
			v = 1 + esp_random() % 6;
		} while(v == dice[i].value);
		setFace(dice[i], v);
	}
}

void DiceScreen::startRoll(){
	armed = false;
	stillCount = 0;
	shakeFlag = false;
	rolling = true;
	rollStart = millis();
	lastTick = 0;
	lv_label_set_text(totalLabel, "...");
	lv_obj_align(totalLabel, LV_ALIGN_BOTTOM_MID, 0, -16);
}

void DiceScreen::finishRoll(){
	rolling = false;
	rollEnd = millis();
	randomizeFaces();
	updateLabels();
	if(audio){
		audio->play({ Chirp{ 800, 1200, 60 }, Chirp{ 1200, 1600, 100 } });
	}
}

void DiceScreen::updateLabels(){
	lv_label_set_text_fmt(countLabel, "%dd6  (UP/DN)", diceCount);
	lv_obj_align(countLabel, LV_ALIGN_TOP_MID, 0, 4);

	uint16_t total = 0;
	for(uint8_t i = 0; i < diceCount; i++){
		total += dice[i].value;
	}
	if(diceCount == 1){
		lv_label_set_text_fmt(totalLabel, "%d", total);
	}else{
		lv_label_set_text_fmt(totalLabel, "= %d", total);
	}
	lv_obj_align(totalLabel, LV_ALIGN_BOTTOM_MID, 0, -16);
}
