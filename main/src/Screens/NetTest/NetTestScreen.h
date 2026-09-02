#ifndef CLOCKSTAR_FIRMWARE_NETTESTSCREEN_H
#define CLOCKSTAR_FIRMWARE_NETTESTSCREEN_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../../LV_Interface/LVScreen.h"
#include "../../Services/WiFiHome.h"
#include "Util/Events.h"
#include "Util/Threaded.h"

/**
 * Network spike / diagnostics: connects to the home WiFi, syncs the clock over SNTP,
 * fetches STATUS_URL over TLS, and reports timings + heap on screen and on the serial
 * log. Reached from the status face with Up. Alt returns home. Select re-runs.
 */
class NetTestScreen : public LVScreen {
public:
	NetTestScreen();
	~NetTestScreen() override;

private:
	static constexpr uint8_t MaxLines = 11;
	lv_obj_t* bg;
	lv_obj_t* lines[MaxLines];

	std::unique_ptr<WiFiHome> wifi;
	ThreadedClosure worker;
	std::atomic<bool> workerDone = false;
	std::atomic<bool> runRequested = true;

	std::mutex logMutex;
	std::vector<std::string> log;
	bool dirty = false;
	void say(const std::string& s);

	EventQueue queue;

	void onStart() override;
	void onStop() override;
	void loop() override;
	void workerFunc();
	void runSequence();
	void repaint();
	void goHome();
};

#endif //CLOCKSTAR_FIRMWARE_NETTESTSCREEN_H
