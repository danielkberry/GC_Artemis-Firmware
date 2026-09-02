#ifndef CLOCKSTAR_FIRMWARE_STATUSSERVICE_H
#define CLOCKSTAR_FIRMWARE_STATUSSERVICE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include "StatusData.h"
#include "WiFiHome.h"
#include "Util/Events.h"
#include "Util/Threaded.h"

/**
 * Keeps the box status current: joins the home WiFi at boot and keeps it up, syncs the
 * clock once over SNTP, then fetches STATUS_URL every FetchInterval on its own thread.
 * Each parsed result is stored (getLatest) and announced as a Facility::Status event.
 *
 * The watch is USB-powered on the box, so WiFi stays associated permanently; joining is
 * the expensive part (~7 s), a fetch is ~1.3 s. On sleep the radio is stopped and it
 * reconnects on wake.
 */
class StatusService : private Threaded {
public:
	StatusService();
	~StatusService() override;

	enum class NetState { Off, Connecting, Connected, Failed };

	struct Event {
		enum { Updated, FetchFailed, NetChanged, NewAlert } action;
	};

	/** Fire the new-alert buzz + LED pattern by hand (diagnostics). */
	void alertFeedback();

	/** Copy of the newest successfully parsed payload. False if none yet. */
	bool getLatest(StatusData& out);

	NetState getNetState() const { return netState; }
	int8_t getRssi();
	uint32_t getLastFetchMs() const { return lastFetchMs; }
	uint32_t getConsecutiveFailures() const { return failures; }
	const char* getLastError();

	/** Fetch as soon as possible instead of waiting for the interval. */
	void requestRefresh() { refreshNow = true; }

	static constexpr uint32_t FetchInterval = 30000; // [ms]

	/** Parse a status.json body. Returns false if it is not the expected document. */
	static bool parse(const std::string& body, StatusData& out);

private:
	void loop() override;

	std::unique_ptr<WiFiHome> wifi;
	std::atomic<NetState> netState = NetState::Off;
	std::atomic<bool> refreshNow = false;
	std::atomic<bool> paused = false;   // asleep: radio off
	uint32_t nextConnectAt = 0;
	uint32_t connectBackoff = 2000;
	uint32_t connectStartedAt = 0;
	uint32_t nextFetchAt = 0;
	std::atomic<uint32_t> lastFetchMs = 0;
	std::atomic<uint32_t> failures = 0;
	bool clockSynced = false;

	std::mutex mutex;
	StatusData latest;
	bool haveLatest = false;
	std::string lastError;

	EventQueue events;
	void handleEvents();
	void startConnect();
	void dropWifi();
	void doFetch();
	void setNet(NetState s);
	void checkAlerts(const StatusData& d);
	bool alertBaselineSet = false;
	uint32_t seenAlertSince = 0;

	static uint32_t millis();
};

#endif //CLOCKSTAR_FIRMWARE_STATUSSERVICE_H
