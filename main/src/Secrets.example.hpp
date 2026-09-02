// Copy to Secrets.hpp (gitignored) and fill in. Secrets.hpp is included by the
// network code; the build fails without it, deliberately.
#ifndef CLOCKSTAR_FIRMWARE_SECRETS_HPP
#define CLOCKSTAR_FIRMWARE_SECRETS_HPP

#define HOME_WIFI_SSID "your-2.4GHz-ssid"   // ESP32-S3 is 2.4 GHz only
#define HOME_WIFI_PASS "your-password"
#define STATUS_URL     "https://status.lan.danielberry.io/status.json"
#define SNTP_SERVER    "pool.ntp.org"

#endif //CLOCKSTAR_FIRMWARE_SECRETS_HPP
