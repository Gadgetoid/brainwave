// IOCTL codes and the state structure shared by bwpower.dll and the app.
#ifndef BRAINWAVE_POWER_H
#define BRAINWAVE_POWER_H

#include <windows.h>

#define BRAINWAVE_POWER_DEVICE L"BWP1:"
#define BRAINWAVE_POWER_IOCTL_READ 0x200
#define BRAINWAVE_POWER_IOCTL_USE_BATTERY 0x201
#define BRAINWAVE_POWER_IOCTL_LOW_VBUS 0x202

#define BRAINWAVE_POWER_CHANGED_DCDC4P2 0x1
#define BRAINWAVE_POWER_CHANGED_CHARGE_4P2 0x2
#define BRAINWAVE_POWER_CHANGED_CHARGER 0x4
#define BRAINWAVE_POWER_CHANGED_VBUS_THRESHOLD 0x8

typedef struct {
    DWORD control;
    DWORD five_volt_control;
    DWORD charge;
    DWORD dcdc_4p2;
    DWORD status;
    DWORD battery_monitor;
    DWORD changed;
    DWORD applied_count;
} brainwave_power_state;

#endif
