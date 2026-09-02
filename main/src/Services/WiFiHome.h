#ifndef CLOCKSTAR_FIRMWARE_WIFIHOME_H
#define CLOCKSTAR_FIRMWARE_WIFIHOME_H

#include <cstdint>
#include <string>
#include <esp_event.h>
#include <esp_netif_types.h>

/**
 * Station-mode WiFi for the home network (DHCP), as opposed to PerseCtrl/WiFiSTA which
 * is hard-wired to the rover's access point. Bring up with connect(), tear down with
 * stop(). Progress is posted as Facility::WiFi events; getState() is the poll interface.
 *
 * Only one instance may exist at a time (it owns the esp_wifi driver).
 */
class WiFiHome {
public:
	WiFiHome();
	~WiFiHome();

	enum class State { Idle, Connecting, Connected, Failed };

	struct Event {
		enum { Connected, Failed, Disconnected } action;
	};

	void connect(const char* ssid, const char* pass);
	void stop();

	State getState() const { return state; }
	const char* ip() const { return ipStr; }
	int8_t rssi() const;

	/** Timings from connect(): ms to association and to DHCP lease. 0 until reached. */
	uint32_t msToAssoc = 0;
	uint32_t msToIp = 0;

private:
	esp_netif_t* netif = nullptr;
	esp_event_handler_instance_t wifiHandler = nullptr;
	esp_event_handler_instance_t ipHandler = nullptr;

	volatile State state = State::Idle;
	char ipStr[16] = "";
	uint32_t connectStart = 0;
	uint8_t retries = 0;
	static constexpr uint8_t MaxRetries = 3;

	void onWifiEvent(int32_t id, void* data);
	void onIpEvent(int32_t id, void* data);
	static uint32_t millis();
};

#endif //CLOCKSTAR_FIRMWARE_WIFIHOME_H
