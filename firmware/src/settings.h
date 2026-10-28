// Saved WiFi networks.
#pragma once

#include <cstddef>
#include <cstdint>

namespace settings {

constexpr size_t network_limit = 8;
constexpr size_t ssid_bytes = 32;
constexpr size_t password_bytes = 64;

struct Network {
    uint8_t ssid_length;
    uint8_t ssid[ssid_bytes];
    uint8_t password_length;
    uint8_t password[password_bytes];
};

void load();
size_t count();
const Network &network(size_t index);
int find(const uint8_t *ssid, size_t ssid_length);
void remember(const Network &network);
void promote(size_t index);
bool forget(const uint8_t *ssid, size_t ssid_length);

}
