// The Advanced tab: Brain power options, Debug Agent forwarding, USB storage, module firmware update and the module log.
#include "ui_advanced.h"

#include <commctrl.h>

#include "app.h"
#include "firmware_update.h"
#include "model.h"
#include "settings.h"
#include "text.h"
#include "ui_style.h"

namespace {

constexpr const wchar_t *advanced_class = L"BrainwaveAdvanced";
constexpr const wchar_t *battery_setting = L"BrainOnBattery";
constexpr const wchar_t *low_vbus_setting = L"LowVbus";
constexpr int battery_check_id = 411;
constexpr int storage_button_id = 412;
constexpr int log_edit_id = 413;
constexpr int log_refresh_id = 414;
constexpr int quit_button_id = 415;
constexpr int low_vbus_check_id = 416;
constexpr int debug_check_id = 417;
constexpr int firmware_button_id = 418;
constexpr int log_characters = module_log_bytes;

constexpr const wchar_t *power_group_label = L"Brain power";
constexpr const wchar_t *debug_group_label = L"Debugging";
constexpr const wchar_t *usb_group_label = L"USB";
constexpr const wchar_t *log_group_label = L"Module log";
constexpr const wchar_t *firmware_group_label = L"Module firmware";
constexpr const wchar_t *firmware_button_label = L"Update Module Firmware";
constexpr const wchar_t *battery_label = L"Power the Brain from its own battery";
constexpr const wchar_t *low_vbus_label = L"Keep USB working down to 3.0 V";
constexpr const wchar_t *debug_label = L"Allow Debug Agent from the WiFi network (port 2100)";
constexpr const wchar_t *debug_warning = L"Anyone on the WiFi network can then copy files to the Brain and run programs on it.";
constexpr const wchar_t *storage_label = L"Switch USB to Storage";
constexpr const wchar_t *quit_label = L"Quit Brainwave WiFi";
constexpr const wchar_t *refresh_label = L"Refresh";
constexpr const wchar_t *usb_explanation =
    L"The Brain's USB port is set to networking for Brainwave. Switch it back to storage to copy files from a computer.";

HWND panel = nullptr;
HWND power_group = nullptr;
HWND debug_group = nullptr;
HWND usb_group = nullptr;
HWND log_group = nullptr;
HWND battery_check = nullptr;
HWND low_vbus_check = nullptr;
HWND power_description = nullptr;
HWND debug_check = nullptr;
HWND debug_text = nullptr;
HWND usb_text = nullptr;
HWND storage_button = nullptr;
HWND log_edit = nullptr;
HWND log_refresh = nullptr;
HWND quit_button = nullptr;
HWND firmware_group = nullptr;
HWND firmware_running = nullptr;
HWND firmware_file = nullptr;
HWND firmware_progress = nullptr;
HWND firmware_note = nullptr;
HWND firmware_button = nullptr;
int shown_progress = -1;
bool battery_wanted = true;
bool low_vbus_wanted = false;
wchar_t log_text[log_characters];

int place_static(HWND control, const wchar_t *text, int left, int top, int width) {
    int height = measure_wrapped_height(control, text, width);
    MoveWindow(control, left, top, width, height, TRUE);
    return top + height;
}

int place_check(HWND check, const wchar_t *label, int left, int top, int width) {
    int height = check_height_for(check, label, width);
    MoveWindow(check, left, top, width, height, TRUE);
    return top + height;
}

int place_button(HWND button, const wchar_t *label, int left, int top) {
    MoveWindow(button, left, top, button_width_for(button, label), style.button_height, TRUE);
    return top + style.button_height;
}

int close_group(HWND group, int left, int top, int width, int content_bottom) {
    int bottom = content_bottom + style.margin;
    MoveWindow(group, left, top, width, bottom - top, TRUE);
    return bottom;
}

void layout(int width, int height) {
    int gap = style.margin;
    int column_width = (width - gap * 3) / 2;
    int inset = style.margin * 2;

    int left = gap;
    int inner_left = left + inset;
    int inner_width = column_width - inset * 2;
    int top = gap / 2;
    int group_top = top;
    top += style.line_height;
    top = place_check(battery_check, battery_label, inner_left, top, inner_width);
    top = place_check(low_vbus_check, low_vbus_label, inner_left, top, inner_width);
    int description_height = measure_wrapped_height(power_description, L"Running from its own battery, not charging. Battery 4.00 V. "
                                                                       L"Restart the Brain to enable low-voltage USB.", inner_width);
    MoveWindow(power_description, inner_left, top + style.margin / 2, inner_width, description_height, TRUE);
    top += style.margin / 2 + description_height;
    top = close_group(power_group, left, group_top, column_width, top);

    group_top = top + gap / 2;
    top = group_top + style.line_height;
    top = place_check(debug_check, debug_label, inner_left, top, inner_width);
    top = place_static(debug_text, debug_warning, inner_left, top, inner_width);
    close_group(debug_group, left, group_top, column_width, top);
    place_button(quit_button, quit_label, left, height - gap - style.button_height);

    int right = gap * 2 + column_width;
    inner_left = right + inset;
    top = gap / 2;
    group_top = top;
    top += style.line_height;
    top = place_static(usb_text, usb_explanation, inner_left, top, inner_width);
    top = place_button(storage_button, storage_label, inner_left, top + style.margin);
    top = close_group(usb_group, right, group_top, column_width, top);

    group_top = top + gap / 2;
    top = group_top + style.line_height;
    MoveWindow(firmware_running, inner_left, top, inner_width, style.line_height, TRUE);
    top += style.line_height;
    MoveWindow(firmware_file, inner_left, top, inner_width, style.line_height, TRUE);
    top += style.line_height + style.margin / 2;
    int button_width = button_width_for(firmware_button, firmware_button_label);
    MoveWindow(firmware_button, inner_left, top, button_width, style.button_height, TRUE);
    int progress_left = inner_left + button_width + style.margin;
    int progress_height = style.cell_height;
    MoveWindow(firmware_progress, progress_left, top + (style.button_height - progress_height) / 2, inner_left + inner_width - progress_left,
               progress_height, TRUE);
    top += style.button_height + style.margin / 2;
    MoveWindow(firmware_note, inner_left, top, inner_width, style.line_height, TRUE);
    top = close_group(firmware_group, right, group_top, column_width, top + style.line_height);

    group_top = top + gap / 2;
    int log_bottom = height - gap / 2;
    MoveWindow(log_group, right, group_top, column_width, log_bottom - group_top, TRUE);
    int refresh_top = group_top + style.line_height;
    place_button(log_refresh, refresh_label, inner_left, refresh_top);
    int edit_top = refresh_top + style.button_height + style.margin;
    MoveWindow(log_edit, inner_left, edit_top, inner_width, log_bottom - style.margin - edit_top, TRUE);
}

LRESULT CALLBACK advanced_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case battery_check_id:
            battery_wanted = SendMessageW(battery_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            settings_write(battery_setting, battery_wanted ? 1 : 0);
            append_log(battery_wanted ? L"brain on battery: on" : L"brain on battery: off");
            break;
        case low_vbus_check_id:
            low_vbus_wanted = SendMessageW(low_vbus_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            settings_write(low_vbus_setting, low_vbus_wanted ? 1 : 0);
            append_log(low_vbus_wanted ? L"low vbus: on" : L"low vbus: off");
            break;
        case debug_check_id: {
            bool wanted = SendMessageW(debug_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            append_log(wanted ? L"debug-forward on" : L"debug-forward off");
            request_command(wanted ? "debug-forward on" : "debug-forward off");
            break;
        }
        case storage_button_id:
            PostMessageW(main_window, WM_COMMAND, advanced_storage_command, 0);
            break;
        case quit_button_id:
            PostMessageW(main_window, WM_COMMAND, advanced_quit_command, 0);
            break;
        case log_refresh_id:
            request_log();
            break;
        case firmware_button_id:
            PostMessageW(main_window, WM_COMMAND, advanced_firmware_command, 0);
            break;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

HWND advanced_create(HWND parent) {
    WNDCLASSW description = {};
    description.lpfnWndProc = advanced_procedure;
    description.hInstance = program_instance;
    description.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    description.lpszClassName = advanced_class;
    RegisterClassW(&description);
    panel = CreateWindowExW(0, advanced_class, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent, nullptr, program_instance, nullptr);
    power_group = create_control(panel, L"BUTTON", power_group_label, BS_GROUPBOX, 0);
    debug_group = create_control(panel, L"BUTTON", debug_group_label, BS_GROUPBOX, 0);
    usb_group = create_control(panel, L"BUTTON", usb_group_label, BS_GROUPBOX, 0);
    log_group = create_control(panel, L"BUTTON", log_group_label, BS_GROUPBOX, 0);
    firmware_group = create_control(panel, L"BUTTON", firmware_group_label, BS_GROUPBOX, 0);
    firmware_running = create_control(panel, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    firmware_file = create_control(panel, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    firmware_note = create_control(panel, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    firmware_button = create_control(panel, L"BUTTON", firmware_button_label, BS_PUSHBUTTON | WS_TABSTOP, firmware_button_id);
    firmware_progress = create_control(panel, PROGRESS_CLASSW, L"", 0, 0);
    SendMessageW(firmware_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    battery_wanted = settings_read(battery_setting, 1) != 0;
    battery_check = create_control(panel, L"BUTTON", battery_label, BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, battery_check_id);
    SendMessageW(battery_check, BM_SETCHECK, battery_wanted ? BST_CHECKED : BST_UNCHECKED, 0);
    low_vbus_wanted = settings_read(low_vbus_setting, 0) != 0;
    low_vbus_check = create_control(panel, L"BUTTON", low_vbus_label, BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, low_vbus_check_id);
    SendMessageW(low_vbus_check, BM_SETCHECK, low_vbus_wanted ? BST_CHECKED : BST_UNCHECKED, 0);
    power_description = create_control(panel, L"STATIC", L"Checking how the Brain is powered...", SS_LEFT | SS_NOPREFIX, 0);
    debug_check = create_control(panel, L"BUTTON", debug_label, BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, debug_check_id);
    debug_text = create_control(panel, L"STATIC", debug_warning, SS_LEFT | SS_NOPREFIX, 0);
    usb_text = create_control(panel, L"STATIC", usb_explanation, SS_LEFT | SS_NOPREFIX, 0);
    storage_button = create_control(panel, L"BUTTON", storage_label, BS_PUSHBUTTON | WS_TABSTOP, storage_button_id);
    quit_button = create_control(panel, L"BUTTON", quit_label, BS_PUSHBUTTON | WS_TABSTOP, quit_button_id);
    log_refresh = create_control(panel, L"BUTTON", refresh_label, BS_PUSHBUTTON | WS_TABSTOP, log_refresh_id);
    log_edit = create_control(panel, L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, log_edit_id);
    send_group_boxes_to_back(panel);
    return panel;
}

void update_firmware_controls() {
    wchar_t text[128];
    wchar_t version[16];
    wchar_t slot[16];
    if (model.status.reachable && model.status.version[0]) {
        ascii_to_wide(model.status.version, version, 16);
        ascii_to_wide(model.status.slot[0] ? model.status.slot : "-", slot, 16);
        wsprintfW(text, L"Running: %s, slot %s", version, slot);
    } else {
        lstrcpyW(text, L"Running: unknown, Brainwave is not connected");
    }
    set_text_if_changed(firmware_running, text);
    const firmware_file_info &file = firmware_probe();
    if (file.present) {
        wsprintfW(text, L"brainwave.bin: %s, %u KB", file.version, static_cast<unsigned>((file.size + 1023) / 1024));
    } else {
        lstrcpyW(text, L"No brainwave.bin next to Brainwave WiFi.");
    }
    set_text_if_changed(firmware_file, text);
    set_text_if_changed(firmware_note, model.update_note);
    bool busy = model.update != update_phase::idle && model.update != update_phase::finished && model.update != update_phase::failed;
    EnableWindow(firmware_button, file.present && model.status.reachable && !busy && !firmware_update_running());
    int progress = busy || model.update == update_phase::finished ? model.update_percent : 0;
    if (progress != shown_progress) {
        shown_progress = progress;
        SendMessageW(firmware_progress, PBM_SETPOS, progress, 0);
    }
}

void advanced_update() {
    update_firmware_controls();
    set_text_if_changed(power_description, model.power_description);
    bool reachable = model.status.reachable;
    EnableWindow(debug_check, reachable);
    UINT wanted = reachable && model.status.debug_forward ? BST_CHECKED : BST_UNCHECKED;
    if (static_cast<UINT>(SendMessageW(debug_check, BM_GETCHECK, 0, 0)) != wanted) {
        SendMessageW(debug_check, BM_SETCHECK, wanted, 0);
    }
}

void advanced_set_log(const char *text) {
    int length = 0;
    for (; text[length] && length + 1 < log_characters; length++) {
        log_text[length] = static_cast<unsigned char>(text[length]);
    }
    log_text[length] = 0;
    SetWindowTextW(log_edit, length ? log_text : L"The module has not logged anything yet.");
    SendMessageW(log_edit, EM_SETSEL, length, length);
    SendMessageW(log_edit, EM_SCROLLCARET, 0, 0);
}

bool advanced_battery_wanted() {
    return battery_wanted;
}

bool advanced_low_vbus_wanted() {
    return low_vbus_wanted;
}

void advanced_shown(bool shown) {
    if (shown) {
        request_log();
    }
}
