// WiFi station management: scanning, joining and status.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wifi {

constexpr size_t scan_limit = 32;

struct AccessPoint {
    uint8_t ssid_length;
    uint8_t ssid[32];
    int16_t rssi;
    uint8_t security;
    uint8_t channel;
};

void init();
void task(uint32_t now_ms);
const char *state_name();
bool connected();
bool joining();
bool current_ssid(uint8_t *ssid, size_t &length);
const AccessPoint *current_access_point();
bool bssid(uint8_t *address);
bool mac_address(uint8_t *address);
uint32_t netmask();
int32_t rssi();
uint32_t address();
uint32_t gateway();
uint32_t dns_server();

bool start_scan();
bool scan_running();
uint32_t scan_generation();
size_t scan_count();
const AccessPoint &scan_result(size_t index);
const char *security_name(uint8_t security);

void join(const uint8_t *ssid, size_t ssid_length, const uint8_t *password, size_t password_length);
bool connect_saved(const uint8_t *ssid, size_t ssid_length);
bool forget(const uint8_t *ssid, size_t ssid_length);
void disconnect();

}
