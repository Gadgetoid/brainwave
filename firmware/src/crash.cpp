// Records panics and hard faults in the persistent log and watchdog scratch registers, reboots, and reports them on the next boot.
#include "crash.h"

#include <cstdarg>
#include <cstdio>

#include "hardware/structs/powman.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

#include "log.h"

namespace {

constexpr uint32_t watchdog_ms = 15000;
constexpr uint32_t crash_magic = 0x43524153;
constexpr uint32_t panic_magic = 0x50414e43;

void report_reset_reason() {
    uint32_t reason = powman_hw->chip_reset;
    log_line("reset reason %08lx:%s%s%s%s%s", static_cast<unsigned long>(reason), (reason & POWMAN_CHIP_RESET_HAD_POR_BITS) ? " power-on" : "",
             (reason & POWMAN_CHIP_RESET_HAD_BOR_BITS) ? " brownout" : "", (reason & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) ? " run-pin" : "",
             (reason & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS)) ? " watchdog" : "",
             (reason & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS) ? " glitch" : "");
}

void report_previous_run() {
    if (watchdog_hw->scratch[0] == panic_magic) {
        log_line("previous run panicked: called from %08lx", static_cast<unsigned long>(watchdog_hw->scratch[1]));
    } else if (watchdog_hw->scratch[0] == crash_magic) {
        log_line("previous run crashed: pc %08lx lr %08lx", static_cast<unsigned long>(watchdog_hw->scratch[1]),
                 static_cast<unsigned long>(watchdog_hw->scratch[2]));
    } else if (watchdog_caused_reboot()) {
        log_line("previous run hung: watchdog reset");
    }
    watchdog_hw->scratch[0] = 0;
}

}

void crash_report_boot() {
    report_reset_reason();
    report_previous_run();
}

void crash_watchdog_start() {
    watchdog_enable(watchdog_ms, true);
}

void crash_watchdog_feed() {
    if (BRAINWAVE_TEST_HANG_AFTER_MS && to_ms_since_boot(get_absolute_time()) > BRAINWAVE_TEST_HANG_AFTER_MS) {
        log_line("test: hanging on purpose");
        for (;;) {
        }
    }
    watchdog_update();
}

extern "C" void __attribute__((noreturn)) brainwave_panic(const char *format, ...) {
    char message[120];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format ? format : "", arguments);
    va_end(arguments);
    log_raw("**** panic: ");
    log_raw(message);
    log_raw("\n");
    watchdog_hw->scratch[0] = panic_magic;
    watchdog_hw->scratch[1] = reinterpret_cast<uint32_t>(__builtin_return_address(0));
    watchdog_reboot(0, 0, 0);
    for (;;) {
    }
}

extern "C" void hardfault_record(const uint32_t *frame) {
    log_raw("**** hard fault\n");
    watchdog_hw->scratch[0] = crash_magic;
    watchdog_hw->scratch[1] = frame[6];
    watchdog_hw->scratch[2] = frame[5];
    watchdog_reboot(0, 0, 0);
    for (;;) {
    }
}

extern "C" __attribute__((naked)) void isr_hardfault() {
    asm volatile("tst lr, #4\n"
                 "ite eq\n"
                 "mrseq r0, msp\n"
                 "mrsne r0, psp\n"
                 "b hardfault_record\n");
}
