// Brainwave WiFi's main window: the tab control, status bar and tray icon, and the dispatch of results from the worker thread.
#include <windows.h>
#include <commctrl.h>
#include <winsock.h>
#include <string.h>

#include "app.h"
#include "firmware_update.h"
#include "model.h"
#include "power_policy.h"
#include "resume.h"
#include "settings.h"
#include "text.h"
#include "tray.h"
#include "ui_advanced.h"
#include "ui_details.h"
#include "ui_networks.h"
#include "ui_style.h"
#include "update_flow.h"
#include "usb_activation.h"

namespace {

constexpr const wchar_t *window_class = L"BrainwaveWindow";
constexpr WORD app_icon_resource = 1;
constexpr const wchar_t *instance_mutex_name = L"Brainwave";
constexpr const wchar_t *tab_setting = L"LastTab";
constexpr DWORD pending_timeout_milliseconds = 20000;
constexpr int tab_count = 3;
constexpr int tray_open_command = 501;
constexpr int tray_quit_command = 502;

const wchar_t *tab_labels[tab_count] = { L"Networks", L"Details", L"Advanced" };

HWND panels[tab_count] = {};
HWND tab_control = nullptr;
HWND status_bar = nullptr;
int current_tab = 0;
char shown_wifi_state[16] = "";

void show_tab(int tab) {
    if (tab < 0 || tab >= tab_count) {
        tab = 0;
    }
    if (static_cast<int>(SendMessageW(tab_control, TCM_GETCURSEL, 0, 0)) != tab) {
        SendMessageW(tab_control, TCM_SETCURSEL, tab, 0);
    }
    for (int index = 0; index < tab_count; index++) {
        if (index == tab) {
            SetWindowPos(panels[index], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        } else {
            ShowWindow(panels[index], SW_HIDE);
        }
    }
    if (current_tab != tab && current_tab == 0) {
        networks_shown(false);
    }
    current_tab = tab;
    if (tab == 2) {
        advanced_shown(true);
    }
    settings_write(tab_setting, static_cast<DWORD>(tab));
}

void layout(int width, int height) {
    SendMessageW(status_bar, WM_SIZE, 0, 0);
    RECT status_area;
    GetWindowRect(status_bar, &status_area);
    int status_height = status_area.bottom - status_area.top;
    int tab_height = height - status_height;
    MoveWindow(tab_control, 0, 0, width, tab_height, TRUE);
    RECT display = { 0, 0, width, tab_height };
    SendMessageW(tab_control, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));
    for (HWND panel : panels) {
        SetWindowPos(panel, HWND_TOP, display.left, display.top, display.right - display.left, display.bottom - display.top, SWP_NOACTIVATE);
    }
}

void create_tabs() {
    tab_control = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 10, 10, main_window,
                                  nullptr, program_instance, nullptr);
    SendMessageW(tab_control, WM_SETFONT, reinterpret_cast<WPARAM>(style.font), FALSE);
    for (int index = 0; index < tab_count; index++) {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(tab_labels[index]);
        SendMessageW(tab_control, TCM_INSERTITEMW, index, reinterpret_cast<LPARAM>(&item));
    }
    status_bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, main_window, nullptr, program_instance,
                                 nullptr);
    SendMessageW(status_bar, WM_SETFONT, reinterpret_cast<WPARAM>(style.font), FALSE);
}

void update_status_bar() {
    wchar_t text[128];
    networks_status_text(text, 128);
    if (model.scanning && model.status.reachable) {
        int length = lstrlenW(text);
        lstrcpynW(text + length, L" Scanning for networks...", 128 - length);
    }
    wchar_t current[128] = L"";
    SendMessageW(status_bar, SB_GETTEXTW, 0, reinterpret_cast<LPARAM>(current));
    if (lstrcmpW(current, text)) {
        SendMessageW(status_bar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text));
    }
}

void update_tray() {
    bool connected = model_connected();
    int bars = connected ? signal_bars(model_rssi()) : 0;
    wchar_t tip[64];
    if (!model.status.reachable) {
        lstrcpyW(tip, L"Brainwave: not connected");
    } else if (connected) {
        wsprintfW(tip, L"Brainwave: %s, %d%%", model.status.ssid, signal_percent(model_rssi()));
    } else {
        lstrcpyW(tip, L"Brainwave: no WiFi");
    }
    tray_update(main_window, model.status.reachable, connected, bars, tip);
}

void refresh_all() {
    update_status_bar();
    networks_update();
    details_update();
    advanced_update();
    update_tray();
}

void show_window() {
    ShowWindow(main_window, SW_SHOW);
    SetForegroundWindow(main_window);
}

void quit() {
    tray_remove(main_window);
    DestroyWindow(main_window);
}

void switch_to_storage() {
    if (MessageBoxW(main_window, L"Switch the Brain's USB port back to storage? Brainwave WiFi stops until you open it again.", L"Brainwave WiFi",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    usb_restore_storage();
    quit();
}

void show_tray_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, tray_open_command, L"Open Brainwave WiFi");
    AppendMenuW(menu, MF_STRING, tray_quit_command, L"Quit");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(main_window);
    UINT command = TrackPopupMenuEx(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RETURNCMD, point.x, point.y, main_window, nullptr);
    DestroyMenu(menu);
    if (command == tray_open_command) {
        show_window();
    } else if (command == tray_quit_command) {
        quit();
    }
}

void handle_status(module_status *status) {
    if (status->failure[0] && strcmp(status->failure, model.status.failure)) {
        wchar_t failure[80];
        wchar_t line[96];
        ascii_to_wide(status->failure, failure, 80);
        wsprintfW(line, L"status failed: %s", failure);
        append_log(line);
    }
    bool first_reach = status->reachable && !model.status.reachable;
    bool lost = !status->reachable && model.status.reachable;
    model.status = *status;
    model.status_seen = true;
    if (first_reach) {
        append_log(L"module reachable");
        model.usb = usb_state::ready;
        request_known();
        if (!model.scan_seen) {
            networks_start_scan();
        }
    }
    if (lost) {
        append_log(L"module unreachable");
        model.scanning = false;
    }
    if (strcmp(shown_wifi_state, model.status.wifi)) {
        wchar_t state[16];
        wchar_t line[64];
        ascii_to_wide(model.status.wifi, state, 16);
        wsprintfW(line, L"wifi: %s", state);
        if (model.status.reachable) {
            append_log(line);
        }
        int copied = 0;
        for (; copied + 1 < static_cast<int>(sizeof(shown_wifi_state)) && model.status.wifi[copied]; copied++) {
            shown_wifi_state[copied] = model.status.wifi[copied];
        }
        shown_wifi_state[copied] = 0;
        if (!strcmp(model.status.wifi, "connected") || !strcmp(model.status.wifi, "failed")) {
            model.pending_ssid[0] = 0;
            model.pending_ssid_hex[0] = 0;
            request_known();
        }
    }
    if (model.pending_ssid[0] && GetTickCount() - model.pending_tick > pending_timeout_milliseconds) {
        model.pending_ssid[0] = 0;
    }
    model_add_history(model_rssi());
    update_flow_watch_restart();
    power_policy_update();
    refresh_all();
}

void handle_command_result(command_result *result) {
    wchar_t command[32];
    char verb[16] = "";
    int index = 0;
    for (; index + 1 < static_cast<int>(sizeof(verb)) && result->command[index] && result->command[index] != ' '; index++) {
        verb[index] = result->command[index];
    }
    verb[index] = 0;
    ascii_to_wide(verb, command, 32);
    if (!result->ok) {
        wchar_t error[64];
        ascii_to_wide(result->error, error, 64);
        wsprintfW(model.action_note, L"Brainwave refused %s: %s", command, error);
        wchar_t line[128];
        wsprintfW(line, L"%s failed: %s", command, error);
        append_log(line);
        model.pending_ssid[0] = 0;
    }
    if (!strcmp(verb, "forget")) {
        request_scan();
        model.scanning = true;
    }
    request_status_now();
    refresh_all();
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_NOTIFY: {
        NMHDR *header = reinterpret_cast<NMHDR *>(lparam);
        if (header->hwndFrom == tab_control && header->code == TCN_SELCHANGE) {
            show_tab(static_cast<int>(SendMessageW(tab_control, TCM_GETCURSEL, 0, 0)));
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        update_tray();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == advanced_storage_command) {
            switch_to_storage();
        } else if (LOWORD(wparam) == advanced_quit_command) {
            quit();
        } else if (LOWORD(wparam) == advanced_firmware_command) {
            update_flow_start();
        }
        return 0;
    case tray_message:
        if (lparam == WM_LBUTTONUP) {
            if (IsWindowVisible(window)) {
                show_tray_menu();
            } else {
                show_window();
            }
        }
        return 0;
    case update_message: {
        update_report *report = reinterpret_cast<update_report *>(lparam);
        update_flow_report(*report);
        LocalFree(report);
        return 0;
    }
    case show_message:
        show_window();
        return 0;
    case status_message: {
        module_status *status = reinterpret_cast<module_status *>(lparam);
        handle_status(status);
        LocalFree(status);
        return 0;
    }
    case scan_message: {
        module_scan *scan = reinterpret_cast<module_scan *>(lparam);
        model.scanning = false;
        if (scan->ok) {
            model.scan = *scan;
            model.scan_seen = true;
        } else {
            wchar_t error[64];
            wchar_t line[96];
            ascii_to_wide(scan->error, error, 64);
            wsprintfW(line, L"scan failed: %s", error);
            append_log(line);
        }
        LocalFree(scan);
        refresh_all();
        return 0;
    }
    case known_message: {
        module_known *known = reinterpret_cast<module_known *>(lparam);
        if (known->ok) {
            model.known = *known;
        }
        LocalFree(known);
        refresh_all();
        return 0;
    }
    case log_message: {
        module_log_text *log = reinterpret_cast<module_log_text *>(lparam);
        advanced_set_log(log->ok ? log->text : "Could not read the module log: Brainwave is not connected.");
        LocalFree(log);
        return 0;
    }
    case command_message: {
        command_result *result = reinterpret_cast<command_result *>(lparam);
        handle_command_result(result);
        LocalFree(result);
        return 0;
    }
    case WM_DESTROY:
        tray_remove(window);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

void app_refresh_views() {
    refresh_all();
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPWSTR command_line, int) {
    if (command_line && !wcsncmp(command_line, L"power-", 6)) {
        app_find_program_directory();
        power_policy_run_test(command_line);
        return 0;
    }
    bool quit_running = command_line && !lstrcmpiW(command_line, L"quit");
    HANDLE instance_lock = CreateMutexW(nullptr, FALSE, instance_mutex_name);
    if (instance_lock && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(window_class, nullptr);
        if (existing && quit_running) {
            DWORD process_id = 0;
            GetWindowThreadProcessId(existing, &process_id);
            PostMessageW(existing, WM_COMMAND, advanced_quit_command, 0);
            for (int waited = 0; waited < 30 && FindWindowW(window_class, nullptr); waited++) {
                Sleep(100);
            }
            if (FindWindowW(window_class, nullptr)) {
                HANDLE process = OpenProcess(0, FALSE, process_id);
                if (process) {
                    TerminateProcess(process, 0);
                    CloseHandle(process);
                }
            }
        } else if (existing) {
            PostMessageW(existing, show_message, 0, 0);
        }
        CloseHandle(instance_lock);
        return 0;
    }
    if (quit_running) {
        CloseHandle(instance_lock);
        return 0;
    }
    bool start_hidden = command_line && !lstrcmpiW(command_line, L"tray");
    program_instance = instance;
    app_find_program_directory();
    append_log(L"brainwave starting");
    usb_install_rndis_driver();
    usb_activate_rndis();
    resume_watch_start();
    WSADATA winsock;
    if (WSAStartup(MAKEWORD(1, 1), &winsock) != 0) {
        append_log(L"winsock unavailable");
    }
    lstrcpyW(model.power_description, L"Checking how the Brain is powered...");

    RECT work = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    style_create(GetSystemMetrics(SM_CYSCREEN) < GetSystemMetrics(SM_CXSCREEN) ? GetSystemMetrics(SM_CYSCREEN) : GetSystemMetrics(SM_CXSCREEN));

    WNDCLASSW class_description = {};
    class_description.lpfnWndProc = window_procedure;
    class_description.hInstance = instance;
    class_description.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(app_icon_resource));
    class_description.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    class_description.lpszClassName = window_class;
    RegisterClassW(&class_description);
    main_window = CreateWindowExW(0, window_class, L"Brainwave WiFi", WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, work.left, work.top,
                                  work.right - work.left, work.bottom - work.top, nullptr, nullptr, instance, nullptr);
    if (!main_window) {
        append_log(L"window creation failed");
        return 1;
    }
    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&controls)) {
        append_log(L"common controls unavailable");
    }
    create_tabs();
    panels[0] = networks_create(main_window);
    panels[1] = details_create(main_window);
    panels[2] = advanced_create(main_window);
    RECT client;
    GetClientRect(main_window, &client);
    layout(client.right, client.bottom);
    show_tab(static_cast<int>(settings_read(tab_setting, 0)));
    if (!start_hidden) {
        ShowWindow(main_window, SW_SHOW);
        UpdateWindow(main_window);
    }
    power_policy_update();
    refresh_all();
    worker_start();
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    append_log(L"brainwave closing");
    return 0;
}
