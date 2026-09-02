#ifndef CLOCKSTAR_FIRMWARE_STATUSDATA_H
#define CLOCKSTAR_FIRMWARE_STATUSDATA_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * Parsed form of https://status.lan.danielberry.io/status.json (inference-stack:
 * docs/services/status-api.md). Filled by the fetcher (step 4); step 2 uses a sample.
 * Any block may be absent (its `valid` false) when the box could not collect it.
 */
struct StatusData {
	uint32_t ts = 0;          // writer's unix time
	uint32_t receivedMs = 0;  // millis() when this was received on the watch (0 = never)

	struct {
		bool valid = false;
		std::string name;
		uint32_t uptimeS = 0;
		uint8_t cpuPct = 0;      // 1-min all-core average
		uint8_t cores = 0;
		float load1 = 0;
		uint8_t memUsedPct = 0;
		float memTotalGb = 0;
		int16_t cpuTempC = 0;
		uint8_t rootUsedPct = 0;
		float rootFreeGb = 0;
		uint8_t flashUsedPct = 0;
		float flashFreeGb = 0;
	} host;

	struct {
		bool valid = false;
		std::string name;
		uint8_t utilPct = 0;
		int16_t tempC = 0;
		uint16_t vramUsedMb = 0;
		uint16_t vramTotalMb = 0;
		float powerW = 0;
		uint8_t fanPct = 0;
	} gpu;

	struct {
		bool valid = false;
		std::vector<std::string> models;  // names of resident models (usually 0 or 1)
	} ollama;

	struct {
		bool valid = false;
		int16_t lastRc = -1;
		uint32_t lastRunTs = 0;
		uint32_t lastSuccessTs = 0;
		bool running = false;
		uint16_t merged = 0;
		uint16_t failed = 0;
	} binhost;

	struct AlertItem {
		std::string name;
		std::string severity;
		uint32_t since = 0;   // activeAt epoch
	};
	struct {
		bool valid = false;
		uint8_t firing = 0;
		std::vector<std::string> names;
		std::vector<AlertItem> items;
		uint32_t latestSince = 0;  // newest activeAt among firing alerts; moves forward on a new firing
	} alerts;

	struct {
		bool valid = false;
		uint16_t running = 0;
		uint16_t unhealthy = 0;
	} containers;

	std::vector<std::string> errors;

	/** Sample matching a real payload from 2026-09-01, for building the UI offline. */
	static StatusData sample(){
		StatusData d;
		d.ts = 1788321008;
		d.host = { true, "inference", 1415462, 13, 12, 3.02f, 34, 15.5f, 45, 52, 231.3f, 40, 586.9f };
		d.gpu = { true, "GTX 1660", 0, 31, 91, 6144, 5.1f, 46 };
		d.ollama = { true, {} };
		d.binhost = { true, 0, 1788311631, 1788311631, false, 2, 0 };
		d.alerts = { true, 0, {}, {}, 0 };
		d.containers = { true, 56, 0 };
		return d;
	}
};

#endif //CLOCKSTAR_FIRMWARE_STATUSDATA_H
