// Drives a module firmware update from the Advanced tab: confirmation, progress reports and the wait for the restart.
#pragma once

#include "firmware_update.h"

void update_flow_start();
void update_flow_report(const update_report &report);
void update_flow_watch_restart();
