#ifndef CLOCKSTAR_FIRMWARE_STATUSFETCH_H
#define CLOCKSTAR_FIRMWARE_STATUSFETCH_H

#include <cstdint>
#include <string>

/**
 * Blocking helpers for the status face's network path. Call from a worker thread,
 * never from the LVGL loop. WiFi must already be connected (WiFiHome).
 */
namespace StatusFetch {

/** Result of one HTTPS GET of STATUS_URL. */
struct Result {
	bool ok = false;
	int httpStatus = 0;
	std::string body;
	uint32_t ms = 0;            // wall time of the whole request
	uint32_t minFreeInternal = 0; // lowest free internal heap seen during the request
	std::string error;
};

/** True if the RTC/clock is plausibly set (TLS certificate validation needs it). */
bool clockIsSet();

/**
 * One-shot SNTP sync against SNTP_SERVER, up to timeoutMs. On success the system clock
 * and the watch's Time service are set. Returns ms taken, or 0 on failure.
 */
uint32_t syncClock(uint32_t timeoutMs = 6000);

/** GET STATUS_URL over TLS using the built-in certificate bundle. */
Result fetch(uint32_t timeoutMs = 8000);

/** Free heap snapshot for the spike log. */
struct Heap {
	uint32_t total;
	uint32_t internal;
	uint32_t largestInternal;
	uint32_t minEverInternal;
};
Heap heap();

}

#endif //CLOCKSTAR_FIRMWARE_STATUSFETCH_H
