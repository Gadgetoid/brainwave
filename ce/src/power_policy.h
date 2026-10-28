// Decides how the Brain is powered while the module is attached, using the bwpower.dll driver.
#pragma once

bool power_policy_run_test(const wchar_t *command_line);
void power_policy_update();
