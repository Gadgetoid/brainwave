// Watchdog, panic and hard fault handling, and the boot-time report of how the previous run ended.
#pragma once

#include <cstdint>

void crash_report_boot();
void crash_watchdog_start();
void crash_watchdog_feed();
