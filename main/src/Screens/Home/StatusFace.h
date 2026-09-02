#ifndef CLOCKSTAR_FIRMWARE_STATUSFACE_H
#define CLOCKSTAR_FIRMWARE_STATUSFACE_H

#include "../../LV_Interface/LVScreen.h"
#include "../../Services/StatusData.h"
#include "../../Services/Time.h"
#include "../../Services/StatusService.h"
#include "../../UIElements/StatusBar.h"
#include "Util/Events.h"

/**
 * Home screen for the watch mounted on the inference box: clock + the box's status
 * (CPU, RAM, disks, binhost, alerts). Select opens the main menu, Down the GPU page,
 * Up the network diagnostics, Alt sleeps.
 *
 * Data comes from StatusService (Facility::Status events); until the first fetch lands it
 * shows "waiting" placeholders.
 */
class StatusFace : public LVScreen {
public:
	StatusFace();
	~StatusFace() override;

	/** Replace the displayed data and repaint. */
	void setData(const StatusData& data);

private:
	Time& ts;
	StatusService* service;
	EventQueue queue;

	StatusData data;
	bool haveData = false;

	StatusBar* statusBar = nullptr;
	lv_obj_t* bg;
	lv_obj_t* clockLabel;
	lv_obj_t* hostLabel;

	struct BarRow {
		lv_obj_t* label;
		lv_obj_t* bar;
		lv_obj_t* value;
	};
	BarRow cpuRow{};
	BarRow ramRow{};
	BarRow rootRow{};
	BarRow flashRow{};
	lv_obj_t* binhostLabel;
	lv_obj_t* alertsLabel;
	lv_obj_t* footerLabel;

	lv_color_t fg;
	lv_color_t bgColor;
	lv_color_t warn;

	static constexpr lv_coord_t Width = 128;
	static constexpr lv_coord_t StatusBarH = 15;
	static constexpr lv_coord_t RowH = 12;
	static constexpr lv_coord_t LabelW = 30;
	static constexpr lv_coord_t ValueW = 36;

	BarRow makeBarRow(lv_coord_t y, const char* name);
	lv_obj_t* makeTextRow(lv_coord_t y);
	void setBar(BarRow& row, uint8_t pct, const char* value, bool alarm);

	void render();
	void updateClock();
	void updateFooter();

	static constexpr uint32_t ClockInterval = 500; // [ms]
	static constexpr uint32_t FooterInterval = 1000; // [ms]
	uint32_t lastClock = 0;
	uint32_t lastFooter = 0;

	void onStart() override;
	void onStop() override;
	void loop() override;

	static std::string ago(uint32_t seconds);
	static uint32_t millis();
};

#endif //CLOCKSTAR_FIRMWARE_STATUSFACE_H
