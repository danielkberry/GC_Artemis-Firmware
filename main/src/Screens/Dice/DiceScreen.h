#ifndef CLOCKSTAR_FIRMWARE_DICESCREEN_H
#define CLOCKSTAR_FIRMWARE_DICESCREEN_H

#include "../../LV_Interface/LVScreen.h"
#include "../../Devices/IMU.h"
#include "../../Devices/Input.h"
#include "../../Services/ChirpSystem.h"
#include "Util/Events.h"
#include "Util/Threaded.h"
#include <atomic>

/**
 * Dice roller. Select rolls, Up/Down change the number of dice (1-3),
 * shaking the watch also rolls, Alt returns to the main menu.
 *
 * Shake detection runs in its own thread at ~100 Hz (the IMU's data rate),
 * since polling from the LVGL loop (~25 ms) misses most of the short peaks.
 */
class DiceScreen : public LVScreen {
public:
	DiceScreen();
	~DiceScreen() override;

private:
	static constexpr uint8_t MaxDice = 3;
	static constexpr uint8_t PipsPerDie = 7;

	struct Die {
		lv_obj_t* body = nullptr;
		lv_obj_t* pips[PipsPerDie] = {};
		uint8_t value = 1;
	};

	Die dice[MaxDice];
	uint8_t diceCount = 2;

	lv_obj_t* bg;
	lv_obj_t* countLabel;
	lv_obj_t* totalLabel;
	lv_obj_t* hintLabel;

	lv_color_t dieColor;
	lv_color_t pipColor;

	IMU* imu;
	ChirpSystem* audio;
	EventQueue queue;

	// Rolling animation state
	bool rolling = false;
	uint32_t rollStart = 0;
	uint32_t lastTick = 0;
	static constexpr uint32_t RollDuration = 700; // [ms]
	static constexpr uint32_t RollTickPeriod = 60; // [ms]

	// Shake detection (reader thread)
	ThreadedClosure reader;
	void readerFunc();
	std::atomic<bool> shakeFlag = false;
	static constexpr TickType_t ReaderDelay = 10 / portTICK_PERIOD_MS;
	static constexpr double Gravity = 1.0; // [g] - IMU::getSample() returns g despite the m/s^2 comment in IMU.h
	static constexpr double HitThreshold = 0.8; // [g] deviation from 1 g that counts as a jolt
	static constexpr uint8_t HitsNeeded = 3; // jolts within HitWindow to count as a shake
	static constexpr uint32_t HitWindow = 400; // [ms]
	uint8_t hits = 0;
	uint32_t windowStart = 0;
	// After a shake triggers, the detector disarms until the watch has been
	// still (below HitThreshold) for StillSamples consecutive samples.
	std::atomic<bool> armed = true;
	static constexpr uint8_t StillSamples = 30; // ~300 ms at 100 Hz
	uint8_t stillCount = 0;
	// Shakes are ignored for a while after a roll lands so the result can be read.
	// Select still rolls immediately.
	static constexpr uint32_t ReadHold = 1500; // [ms]
	uint32_t rollEnd = 0;

	void loop() override;
	void onStart() override;
	void onStop() override;

	void handleInput(const Input::Data& evt);

	void layoutDice();
	void setFace(Die& die, uint8_t value);
	void randomizeFaces();
	void startRoll();
	void finishRoll();
	void updateLabels();

	static uint32_t millis();
};

#endif //CLOCKSTAR_FIRMWARE_DICESCREEN_H
