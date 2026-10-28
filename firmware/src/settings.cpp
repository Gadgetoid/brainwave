// Up to 8 networks with passwords, most recently used first, in the last flash sector outside both firmware slots.
#include "settings.h"

#include <cstring>

#include "hardware/flash.h"
#include "pico/flash.h"

#include "log.h"

namespace settings {

namespace {

constexpr uint32_t magic = 0x42577631;
constexpr uint32_t storage_offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct Stored {
    uint32_t magic;
    uint32_t count;
    Network networks[network_limit];
    uint32_t checksum;
};

static_assert(sizeof(Stored) <= FLASH_SECTOR_SIZE, "settings must fit one sector");

Stored current = {};

uint32_t checksum_of(const Stored &stored) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&stored);
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < offsetof(Stored, checksum); index++) {
        hash = (hash ^ bytes[index]) * 16777619u;
    }
    return hash;
}

uint8_t page_buffer[(sizeof(Stored) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE];

void write_flash(void *) {
    flash_range_erase(storage_offset, FLASH_SECTOR_SIZE);
    flash_range_program(storage_offset, page_buffer, sizeof(page_buffer));
}

void save() {
    current.magic = magic;
    current.checksum = checksum_of(current);
    memset(page_buffer, 0xff, sizeof(page_buffer));
    memcpy(page_buffer, &current, sizeof(current));
    int result = flash_safe_execute(write_flash, nullptr, 1000);
    if (result != PICO_OK) {
        log_line("settings: flash write failed (%d)", result);
    }
}

}

void load() {
    const Stored *stored = reinterpret_cast<const Stored *>(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + storage_offset);
    if (stored->magic == magic && stored->count <= network_limit && stored->checksum == checksum_of(*stored)) {
        current = *stored;
    } else {
        current = {};
    }
    log_line("settings: %lu saved networks", static_cast<unsigned long>(current.count));
}

size_t count() {
    return current.count;
}

const Network &network(size_t index) {
    return current.networks[index];
}

int find(const uint8_t *ssid, size_t ssid_length) {
    for (size_t index = 0; index < current.count; index++) {
        const Network &entry = current.networks[index];
        if (entry.ssid_length == ssid_length && !memcmp(entry.ssid, ssid, ssid_length)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void remember(const Network &network) {
    int existing = find(network.ssid, network.ssid_length);
    size_t last = existing >= 0 ? static_cast<size_t>(existing) : (current.count < network_limit ? current.count : network_limit - 1);
    if (existing < 0 && current.count < network_limit) {
        current.count++;
    }
    memmove(&current.networks[1], &current.networks[0], last * sizeof(Network));
    current.networks[0] = network;
    save();
}

void promote(size_t index) {
    if (index == 0 || index >= current.count) {
        return;
    }
    Network chosen = current.networks[index];
    memmove(&current.networks[1], &current.networks[0], index * sizeof(Network));
    current.networks[0] = chosen;
    save();
}

bool forget(const uint8_t *ssid, size_t ssid_length) {
    int existing = find(ssid, ssid_length);
    if (existing < 0) {
        return false;
    }
    size_t index = static_cast<size_t>(existing);
    memmove(&current.networks[index], &current.networks[index + 1], (current.count - index - 1) * sizeof(Network));
    current.count--;
    save();
    return true;
}

}
