// Writes a received image into the other slot, verifies its SHA-256 in flash, restarts on trial and keeps it after 30 s.
#include "updater.h"

#include <cstring>

#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "boot/uf2.h"
#include "hardware/flash.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "pico/sha256.h"
#include "pico/time.h"

#include "log.h"

namespace {

constexpr uint32_t buy_after_ms = 30000;
constexpr uint32_t reboot_delay_ms = 3000;
constexpr size_t sector_bytes = FLASH_SECTOR_SIZE;

uint8_t workarea[4096] __attribute__((aligned(4)));
uint8_t sector_buffer[sector_bytes] __attribute__((aligned(4)));
size_t sector_filled = 0;

bool active = false;
bool buy_pending = false;
bool reboot_scheduled = false;
uint32_t reboot_at_ms = 0;
int boot_partition = -1;

uint32_t target_start = 0;
uint32_t target_size = 0;
uint32_t image_size = 0;
uint32_t written = 0;
uint8_t wanted_hash[SHA256_RESULT_BYTES];
pico_sha256_state_t hash_state;

struct SectorWrite {
    uint32_t offset;
    const uint8_t *data;
    size_t length;
};

void write_sector_now(void *parameter) {
    const SectorWrite *write = static_cast<const SectorWrite *>(parameter);
    flash_range_erase(write->offset, sector_bytes);
    flash_range_program(write->offset, write->data, write->length);
}

bool flush_sector() {
    if (!sector_filled) {
        return true;
    }
    size_t length = (sector_filled + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
    memset(sector_buffer + sector_filled, 0xff, length - sector_filled);
    SectorWrite write = {target_start + written - static_cast<uint32_t>(sector_filled), sector_buffer, length};
    int result = flash_safe_execute(write_sector_now, &write, 2000);
    sector_filled = 0;
    return result == PICO_OK;
}

bool hash_flash(uint8_t *result) {
    pico_sha256_state_t state;
    if (pico_sha256_start_blocking(&state, SHA256_BIG_ENDIAN, false) != PICO_OK) {
        return false;
    }
    const uint8_t *flash = reinterpret_cast<const uint8_t *>(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + target_start);
    pico_sha256_update_blocking(&state, flash, image_size);
    sha256_result_t digest;
    pico_sha256_finish(&state, &digest);
    memcpy(result, digest.bytes, SHA256_RESULT_BYTES);
    return true;
}

}

void updater_init() {
    boot_info_t info = {};
    if (rom_get_boot_info(&info)) {
        boot_partition = info.partition;
        buy_pending = (info.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) != 0;
    }
    log_line("update: running from slot %s%s%s", updater_slot_name(), rom_get_last_boot_type() == BOOT_TYPE_FLASH_UPDATE ? ", after an update" : "",
             buy_pending ? ", on trial" : "");
}

const char *updater_slot_name() {
    switch (boot_partition) {
    case 0:
        return "A";
    case 1:
        return "B";
    default:
        return "unpartitioned";
    }
}

void updater_task(uint32_t now_ms) {
    if (buy_pending && now_ms > buy_after_ms) {
        buy_pending = false;
        int result = rom_explicit_buy(workarea, sizeof(workarea));
        log_line("update: kept the new firmware (%d)", result);
    }
    if (reboot_scheduled && (int32_t)(now_ms - reboot_at_ms) >= 0) {
        reboot_scheduled = false;
        log_line("update: restarting into the new firmware");
        rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 100, XIP_BASE + target_start, 0);
    }
}

bool updater_begin(uint32_t size, const uint8_t *expected_hash, const char *&error) {
    if (active || reboot_scheduled) {
        error = "update already in progress";
        return false;
    }
    if (boot_partition < 0) {
        error = "no partition table; flash partitions.uf2 first";
        return false;
    }
    resident_partition_t partition = {};
    rom_flash_flush_cache();
    if (rom_get_uf2_target_partition(workarea, sizeof(workarea), RP2350_ARM_S_FAMILY_ID, &partition) < 0) {
        error = "no partition to update";
        return false;
    }
    uint32_t first_sector = (partition.permissions_and_location & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    uint32_t last_sector = (partition.permissions_and_location & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    target_start = first_sector * sector_bytes;
    target_size = (last_sector + 1 - first_sector) * sector_bytes;
    if (!size || size > target_size) {
        error = "image does not fit";
        return false;
    }
    if (pico_sha256_start_blocking(&hash_state, SHA256_BIG_ENDIAN, false) != PICO_OK) {
        error = "hash unavailable";
        return false;
    }
    image_size = size;
    written = 0;
    sector_filled = 0;
    memcpy(wanted_hash, expected_hash, SHA256_RESULT_BYTES);
    active = true;
    log_line("update: receiving %lu bytes into flash at %08lx", static_cast<unsigned long>(size), static_cast<unsigned long>(target_start));
    return true;
}

bool updater_write(const uint8_t *data, size_t length, const char *&error) {
    if (!active) {
        error = "no update in progress";
        return false;
    }
    if (written + length > image_size) {
        updater_abort();
        error = "more data than announced";
        return false;
    }
    pico_sha256_update_blocking(&hash_state, data, length);
    while (length) {
        size_t chunk = sector_bytes - sector_filled;
        if (chunk > length) {
            chunk = length;
        }
        memcpy(sector_buffer + sector_filled, data, chunk);
        sector_filled += chunk;
        written += chunk;
        data += chunk;
        length -= chunk;
        if (sector_filled == sector_bytes && !flush_sector()) {
            updater_abort();
            error = "flash write failed";
            return false;
        }
    }
    return true;
}

bool updater_finish(const char *&error) {
    if (!active || written != image_size) {
        updater_abort();
        error = "incomplete image";
        return false;
    }
    if (!flush_sector()) {
        updater_abort();
        error = "flash write failed";
        return false;
    }
    sha256_result_t received;
    pico_sha256_finish(&hash_state, &received);
    active = false;
    if (memcmp(received.bytes, wanted_hash, SHA256_RESULT_BYTES)) {
        error = "received data does not match the hash";
        log_line("update: hash mismatch in received data");
        return false;
    }
    uint8_t stored[SHA256_RESULT_BYTES];
    if (!hash_flash(stored) || memcmp(stored, wanted_hash, SHA256_RESULT_BYTES)) {
        error = "flash contents do not match the hash";
        log_line("update: hash mismatch after writing");
        return false;
    }
    log_line("update: image written and verified");
    reboot_scheduled = true;
    reboot_at_ms = to_ms_since_boot(get_absolute_time()) + reboot_delay_ms;
    return true;
}

void updater_abort() {
    if (active) {
        sha256_result_t discard;
        pico_sha256_finish(&hash_state, &discard);
        log_line("update: abandoned after %lu bytes", static_cast<unsigned long>(written));
    }
    active = false;
    sector_filled = 0;
}

bool updater_active() {
    return active;
}
