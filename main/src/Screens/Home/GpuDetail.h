#ifndef CLOCKSTAR_FIRMWARE_GPUDETAIL_H
#define CLOCKSTAR_FIRMWARE_GPUDETAIL_H

#include "../../LV_Interface/LVScreen.h"
#include "../../Services/StatusService.h"
#include "Util/Events.h"

/** GPU detail page (Down from the status face): utilisation, VRAM, power, fan, model. Alt/Up: back. */
class GpuDetail : public LVScreen {
public:
	GpuDetail();

private:
	StatusService* service;
	EventQueue queue;
	lv_obj_t* bg;
	static constexpr uint8_t Rows = 9;
	lv_obj_t* rows[Rows];
	lv_obj_t* utilBar;
	lv_obj_t* vramBar;
	lv_color_t fg, warn;

	void render();
	void onStart() override;
	void onStop() override;
	void loop() override;
};

#endif //CLOCKSTAR_FIRMWARE_GPUDETAIL_H
