// bwpower.dll: a stream driver (BWP1:) loaded from the card that maps the i.MX28 power block, reports it, moves the Brain off USB
// power and lowers the VBUS threshold.
#include <windows.h>

#include "brainwave_power.h"

#define POWER_PHYSICAL 0x80044000
#define POWER_BYTES 0x1000
#define REGISTER_SET 0x4
#define POWER_CTRL 0x00
#define POWER_5VCTRL 0x10
#define POWER_CHARGE 0x30
#define POWER_DCDC4P2 0x80
#define POWER_STS 0xc0
#define POWER_BATTMONITOR 0xe0
#define DCDC4P2_ENABLE_DCDC 0x00800000
#define DCDC4P2_ENABLE_4P2 0x00400000
#define FIVE_VOLT_PWD_CHARGE_4P2 0x00300000
#define CHARGE_PWD_BATTCHRG 0x00010000
#define FIVE_VOLT_VBUSVALID_TRSH 0x00000700
#define FIVE_VOLT_VBUSVALID_TO_B 0x00000008
#define REGISTER_CLEAR 0x8

typedef struct {
    volatile BYTE *registers;
    CRITICAL_SECTION lock;
    DWORD applied_count;
    BOOL low_vbus;
    BOOL saved_threshold;
    DWORD original_threshold_bits;
} power_context;

static DWORD read_register(power_context *context, DWORD offset)
{
    return *(volatile DWORD *)(context->registers + offset);
}

static void write_register(power_context *context, DWORD offset, DWORD value)
{
    *(volatile DWORD *)(context->registers + offset) = value;
}

static void fill_state(power_context *context, brainwave_power_state *state)
{
    state->control = read_register(context, POWER_CTRL);
    state->five_volt_control = read_register(context, POWER_5VCTRL);
    state->charge = read_register(context, POWER_CHARGE);
    state->dcdc_4p2 = read_register(context, POWER_DCDC4P2);
    state->status = read_register(context, POWER_STS);
    state->battery_monitor = read_register(context, POWER_BATTMONITOR);
    state->applied_count = context->applied_count;
}

static DWORD use_battery(power_context *context)
{
    DWORD changed = 0;

    DWORD dcdc_4p2 = read_register(context, POWER_DCDC4P2);

    if (dcdc_4p2 & (DCDC4P2_ENABLE_DCDC | DCDC4P2_ENABLE_4P2)) {
        write_register(context, POWER_DCDC4P2, dcdc_4p2 & ~(DCDC4P2_ENABLE_DCDC | DCDC4P2_ENABLE_4P2));
        changed |= BRAINWAVE_POWER_CHANGED_DCDC4P2;
    }
    if ((read_register(context, POWER_5VCTRL) & FIVE_VOLT_PWD_CHARGE_4P2) != FIVE_VOLT_PWD_CHARGE_4P2) {
        write_register(context, POWER_5VCTRL + REGISTER_SET, FIVE_VOLT_PWD_CHARGE_4P2);
        changed |= BRAINWAVE_POWER_CHANGED_CHARGE_4P2;
    }
    if (!(read_register(context, POWER_CHARGE) & CHARGE_PWD_BATTCHRG)) {
        write_register(context, POWER_CHARGE + REGISTER_SET, CHARGE_PWD_BATTCHRG);
        changed |= BRAINWAVE_POWER_CHANGED_CHARGER;
    }
    if (changed)
        context->applied_count++;
    return changed;
}

static DWORD set_low_vbus(power_context *context, BOOL enable)
{
    DWORD five_volt = read_register(context, POWER_5VCTRL);
    DWORD wanted;

    if (enable && !context->saved_threshold) {
        context->original_threshold_bits = five_volt & (FIVE_VOLT_VBUSVALID_TRSH | FIVE_VOLT_VBUSVALID_TO_B);
        context->saved_threshold = TRUE;
    }
    context->low_vbus = enable;
    if (!context->saved_threshold)
        return 0;
    wanted = enable ? FIVE_VOLT_VBUSVALID_TO_B : context->original_threshold_bits;
    if ((five_volt & (FIVE_VOLT_VBUSVALID_TRSH | FIVE_VOLT_VBUSVALID_TO_B)) == wanted)
        return 0;
    write_register(context, POWER_5VCTRL + REGISTER_CLEAR, (FIVE_VOLT_VBUSVALID_TRSH | FIVE_VOLT_VBUSVALID_TO_B) & ~wanted);
    write_register(context, POWER_5VCTRL + REGISTER_SET, wanted);
    return BRAINWAVE_POWER_CHANGED_VBUS_THRESHOLD;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

DWORD BWP_Init(LPCWSTR active_key, const void *bus_context)
{
    power_context *context = (power_context *)LocalAlloc(LPTR, sizeof(power_context));
    void *window;

    if (!context)
        return 0;
    window = VirtualAlloc(NULL, POWER_BYTES, MEM_RESERVE, PAGE_NOACCESS);
    if (!window || !VirtualCopy(window, (void *)(POWER_PHYSICAL >> 8), POWER_BYTES, PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL)) {
        if (window)
            VirtualFree(window, 0, MEM_RELEASE);
        LocalFree(context);
        return 0;
    }
    context->registers = (volatile BYTE *)window;
    InitializeCriticalSection(&context->lock);
    return (DWORD)context;
}

BOOL BWP_Deinit(DWORD handle)
{
    power_context *context = (power_context *)handle;

    if (!context)
        return FALSE;
    VirtualFree((void *)context->registers, 0, MEM_RELEASE);
    DeleteCriticalSection(&context->lock);
    LocalFree(context);
    return TRUE;
}

DWORD BWP_Open(DWORD handle, DWORD access, DWORD share)
{
    return handle;
}

BOOL BWP_Close(DWORD handle)
{
    return TRUE;
}

BOOL BWP_IOControl(DWORD handle, DWORD code, BYTE *input, DWORD input_length, BYTE *output, DWORD output_length, DWORD *returned)
{
    power_context *context = (power_context *)handle;
    brainwave_power_state *state = (brainwave_power_state *)output;

    if (!context || !state || output_length < sizeof(brainwave_power_state)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (code == BRAINWAVE_POWER_IOCTL_LOW_VBUS && (!input || input_length < sizeof(DWORD))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (code != BRAINWAVE_POWER_IOCTL_READ && code != BRAINWAVE_POWER_IOCTL_USE_BATTERY && code != BRAINWAVE_POWER_IOCTL_LOW_VBUS) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    EnterCriticalSection(&context->lock);
    if (code == BRAINWAVE_POWER_IOCTL_USE_BATTERY)
        state->changed = use_battery(context);
    else if (code == BRAINWAVE_POWER_IOCTL_LOW_VBUS)
        state->changed = set_low_vbus(context, *(const DWORD *)input != 0);
    else
        state->changed = 0;
    fill_state(context, state);
    LeaveCriticalSection(&context->lock);
    if (returned)
        *returned = sizeof(brainwave_power_state);
    return TRUE;
}
