// The Networks tab: the scanned and saved networks with signal icons, and Connect, Forget, Disconnect and Refresh.
#include "ui_networks.h"

#include <commctrl.h>
#include <string.h>

#include "app.h"
#include "model.h"
#include "text.h"
#include "ui_password.h"
#include "ui_style.h"

namespace {

constexpr const wchar_t *networks_class = L"BrainwaveNetworks";
constexpr int list_id = 301;
constexpr int connect_button_id = 302;
constexpr int forget_button_id = 303;
constexpr int refresh_button_id = 304;
constexpr int disconnect_button_id = 305;
constexpr int menu_connect_id = 311;
constexpr int menu_forget_id = 312;
constexpr int menu_password_id = 313;
constexpr int row_limit = module_network_limit * 2;
constexpr UINT auto_scan_timer = 1;
constexpr UINT auto_scan_milliseconds = 30000;
constexpr int image_dimmed = 10;
constexpr int column_count = 4;
constexpr int cell_characters = 48;

constexpr const wchar_t *connect_label = L"Connect";
constexpr const wchar_t *forget_label = L"Forget";
constexpr const wchar_t *disconnect_label = L"Disconnect";
constexpr const wchar_t *refresh_label = L"Refresh";
constexpr const wchar_t *scanning_label = L"Scanning...";
const wchar_t *column_titles[column_count] = { L"Network", L"Signal", L"Security", L"Status" };

struct network_row {
    wchar_t ssid[module_ssid_characters];
    char ssid_hex[module_ssid_hex_bytes];
    int rssi;
    char security[8];
    bool saved;
    bool in_range;
    bool connected;
};

struct shown_cells {
    char ssid_hex[module_ssid_hex_bytes];
    int image;
    wchar_t cells[column_count][cell_characters];
};

HWND panel = nullptr;
HWND state_title = nullptr;
HWND state_hint = nullptr;
HWND list = nullptr;
HWND connect_button = nullptr;
HWND forget_button = nullptr;
HWND refresh_button = nullptr;
HWND disconnect_button = nullptr;
HIMAGELIST images = nullptr;

network_row rows[row_limit];
int row_count = 0;
shown_cells shown[row_limit];
int shown_count = -1;

bool is_secured(const char *security) {
    return security[0] && strcmp(security, "open") && strcmp(security, "-");
}

void copy_wide(wchar_t *destination, const wchar_t *source, int characters) {
    int index = 0;
    for (; index + 1 < characters && source[index]; index++) {
        destination[index] = source[index];
    }
    destination[index] = 0;
}

void copy_narrow(char *destination, const char *source, int bytes) {
    int index = 0;
    for (; index + 1 < bytes && source[index]; index++) {
        destination[index] = source[index];
    }
    destination[index] = 0;
}

bool known_contains(const char *ssid_hex) {
    for (int index = 0; index < model.known.count; index++) {
        if (!strcmp(model.known.ssid_hex[index], ssid_hex)) {
            return true;
        }
    }
    return false;
}

int find_row(const char *ssid_hex) {
    for (int index = 0; index < row_count; index++) {
        if (!strcmp(rows[index].ssid_hex, ssid_hex)) {
            return index;
        }
    }
    return -1;
}

int row_rank(const network_row &row) {
    if (row.connected) {
        return 0;
    }
    return row.in_range ? 1 : 2;
}

void sort_rows() {
    for (int outer = 1; outer < row_count; outer++) {
        network_row moving = rows[outer];
        int inner = outer;
        while (inner > 0) {
            const network_row &before = rows[inner - 1];
            bool earlier = row_rank(moving) < row_rank(before) ||
                           (row_rank(moving) == row_rank(before) && moving.in_range && moving.rssi > before.rssi);
            if (!earlier) {
                break;
            }
            rows[inner] = rows[inner - 1];
            inner--;
        }
        rows[inner] = moving;
    }
}

void build_rows() {
    row_count = 0;
    const char *connected_hex = model_connected() ? model.status.ssid_hex : "";
    if (model.scan_seen) {
        for (int index = 0; index < model.scan.count && row_count < row_limit; index++) {
            const module_network &network = model.scan.networks[index];
            network_row &row = rows[row_count++];
            copy_wide(row.ssid, network.ssid, module_ssid_characters);
            copy_narrow(row.ssid_hex, network.ssid_hex, module_ssid_hex_bytes);
            copy_narrow(row.security, network.security, sizeof(row.security));
            row.rssi = network.rssi;
            row.in_range = true;
            row.saved = network.saved || known_contains(network.ssid_hex);
            row.connected = connected_hex[0] && !strcmp(network.ssid_hex, connected_hex);
        }
    }
    for (int index = 0; index < model.known.count && row_count < row_limit; index++) {
        if (find_row(model.known.ssid_hex[index]) >= 0) {
            continue;
        }
        network_row &row = rows[row_count++];
        copy_wide(row.ssid, model.known.ssid[index], module_ssid_characters);
        copy_narrow(row.ssid_hex, model.known.ssid_hex[index], module_ssid_hex_bytes);
        row.security[0] = 0;
        row.rssi = -100;
        row.in_range = false;
        row.saved = true;
        row.connected = false;
    }
    if (connected_hex[0] && strcmp(connected_hex, "-")) {
        int existing = find_row(connected_hex);
        if (existing < 0 && row_count < row_limit) {
            network_row &row = rows[row_count++];
            copy_wide(row.ssid, model.status.ssid, module_ssid_characters);
            copy_narrow(row.ssid_hex, connected_hex, module_ssid_hex_bytes);
            copy_narrow(row.security, model.status.security, sizeof(row.security));
            row.saved = true;
            existing = row_count - 1;
        }
        if (existing >= 0) {
            rows[existing].connected = true;
            rows[existing].in_range = true;
            if (model_rssi() != history_gap) {
                rows[existing].rssi = model_rssi();
            }
        }
    }
    sort_rows();
}

const wchar_t *current_network_name() {
    if (model.status.ssid[0]) {
        return model.status.ssid;
    }
    return model.pending_ssid[0] ? model.pending_ssid : L"the network";
}

void row_cells(const network_row &row, shown_cells &cells) {
    copy_narrow(cells.ssid_hex, row.ssid_hex, module_ssid_hex_bytes);
    int bars = row.in_range ? signal_bars(row.rssi) : 0;
    cells.image = row.in_range ? bars + (is_secured(row.security) ? 5 : 0) : image_dimmed;
    copy_wide(cells.cells[0], row.ssid, cell_characters);
    if (row.in_range) {
        wsprintfW(cells.cells[1], L"%d%%", signal_percent(row.rssi));
    } else {
        lstrcpyW(cells.cells[1], L"-");
    }
    if (!row.security[0] || !strcmp(row.security, "-")) {
        cells.cells[2][0] = 0;
    } else if (!is_secured(row.security)) {
        lstrcpyW(cells.cells[2], L"Open");
    } else {
        ascii_to_wide(row.security, cells.cells[2], cell_characters);
        for (wchar_t *cursor = cells.cells[2]; *cursor; cursor++) {
            if (*cursor >= L'a' && *cursor <= L'z') {
                *cursor = static_cast<wchar_t>(*cursor - L'a' + L'A');
            }
        }
    }
    if (row.connected) {
        lstrcpyW(cells.cells[3], L"Connected");
    } else if (row.saved && !row.in_range) {
        lstrcpyW(cells.cells[3], L"Saved, not in range");
    } else if (row.saved) {
        lstrcpyW(cells.cells[3], L"Saved");
    } else {
        cells.cells[3][0] = 0;
    }
}

int selected_index() {
    int index = static_cast<int>(SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED));
    return index >= 0 && index < row_count ? index : -1;
}

void set_cell(int item, int column, const wchar_t *text) {
    LVITEMW cell = {};
    cell.iSubItem = column;
    cell.pszText = const_cast<wchar_t *>(text);
    SendMessageW(list, LVM_SETITEMTEXTW, item, reinterpret_cast<LPARAM>(&cell));
}

void set_image(int item, int image) {
    LVITEMW cell = {};
    cell.mask = LVIF_IMAGE;
    cell.iItem = item;
    cell.iImage = image;
    SendMessageW(list, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&cell));
}

void insert_item(int item, const shown_cells &cells) {
    LVITEMW entry = {};
    entry.mask = LVIF_TEXT | LVIF_IMAGE;
    entry.iItem = item;
    entry.pszText = const_cast<wchar_t *>(cells.cells[0]);
    entry.iImage = cells.image;
    SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&entry));
    for (int column = 1; column < column_count; column++) {
        set_cell(item, column, cells.cells[column]);
    }
}

void select_item(int item) {
    LVITEMW state = {};
    state.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    state.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, item, reinterpret_cast<LPARAM>(&state));
}

void reconcile_list() {
    static shown_cells wanted[row_limit];
    for (int index = 0; index < row_count; index++) {
        row_cells(rows[index], wanted[index]);
    }
    bool same_order = shown_count == row_count;
    for (int index = 0; same_order && index < row_count; index++) {
        same_order = !strcmp(shown[index].ssid_hex, wanted[index].ssid_hex);
    }
    if (same_order) {
        for (int index = 0; index < row_count; index++) {
            if (wanted[index].image != shown[index].image) {
                set_image(index, wanted[index].image);
            }
            for (int column = 0; column < column_count; column++) {
                if (lstrcmpW(wanted[index].cells[column], shown[index].cells[column])) {
                    set_cell(index, column, wanted[index].cells[column]);
                }
            }
            shown[index] = wanted[index];
        }
        return;
    }
    char selected_hex[module_ssid_hex_bytes] = "";
    int selected = selected_index();
    if (selected >= 0 && selected < shown_count) {
        copy_narrow(selected_hex, shown[selected].ssid_hex, module_ssid_hex_bytes);
    }
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    for (int index = 0; index < row_count; index++) {
        insert_item(index, wanted[index]);
        shown[index] = wanted[index];
        if (selected_hex[0] && !strcmp(selected_hex, wanted[index].ssid_hex)) {
            select_item(index);
        }
    }
    shown_count = row_count;
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

void state_text(wchar_t *title, wchar_t *hint) {
    hint[0] = 0;
    if (!model.status_seen) {
        lstrcpyW(title, L"Starting...");
        return;
    }
    if (!model.status.reachable) {
        lstrcpyW(title, L"Brainwave is not connected.");
        if (model.usb == usb_state::replug_needed) {
            lstrcpyW(hint, L"Unplug Brainwave and plug it in again.");
        } else if (model.usb == usb_state::failed) {
            lstrcpyW(hint, L"The Brain's USB could not be switched to networking.");
        } else {
            lstrcpyW(hint, L"Plug Brainwave into the Brain's USB port.");
        }
        return;
    }
    const char *wifi = model.status.wifi;
    if (!strcmp(wifi, "connected")) {
        wsprintfW(title, L"Connected to %s.", model.status.ssid);
        wchar_t address[16];
        ascii_to_wide(model.status.ip, address, 16);
        wsprintfW(hint, L"IP address %s, signal %d%%.", address, signal_percent(model_rssi()));
    } else if (!strcmp(wifi, "connecting")) {
        wsprintfW(title, L"Connecting to %s...", current_network_name());
    } else if (!strcmp(wifi, "failed")) {
        wsprintfW(title, L"Could not join %s.", current_network_name());
        lstrcpyW(hint, L"Check the password, then double-tap the network to try again.");
    } else if (!strcmp(wifi, "scanning")) {
        lstrcpyW(title, L"Looking for saved networks...");
    } else if (!strcmp(wifi, "disconnected")) {
        lstrcpyW(title, L"Disconnected.");
        lstrcpyW(hint, L"Double-tap a network to connect.");
    } else if (!strcmp(wifi, "not-found")) {
        lstrcpyW(title, L"No saved network in range.");
        lstrcpyW(hint, L"Double-tap a network to join it.");
    } else {
        lstrcpyW(title, L"Not connected.");
        lstrcpyW(hint, L"Double-tap a network to join it.");
    }
    if (model.action_note[0]) {
        copy_wide(hint, model.action_note, 128);
    }
}

void update_buttons() {
    int selected = selected_index();
    bool reachable = model.status.reachable;
    bool can_connect = reachable && selected >= 0 && !rows[selected].connected && rows[selected].in_range;
    bool can_forget = reachable && selected >= 0 && rows[selected].saved;
    EnableWindow(connect_button, can_connect);
    EnableWindow(forget_button, can_forget);
    bool can_disconnect = reachable && (!strcmp(model.status.wifi, "connected") || !strcmp(model.status.wifi, "connecting"));
    EnableWindow(disconnect_button, can_disconnect);
    EnableWindow(refresh_button, reachable && !model.scanning);
    set_text_if_changed(refresh_button, model.scanning ? scanning_label : refresh_label);
}

void set_pending(const network_row &row) {
    copy_wide(model.pending_ssid, row.ssid, module_ssid_characters);
    copy_narrow(model.pending_ssid_hex, row.ssid_hex, module_ssid_hex_bytes);
    model.pending_tick = GetTickCount();
    model.action_note[0] = 0;
}

void send_join(const network_row &row, const char *password_hex) {
    char command[command_bytes];
    build_command(command, "join", row.ssid_hex, password_hex);
    set_pending(row);
    wchar_t line[96];
    wsprintfW(line, L"join %s", row.ssid);
    append_log(line);
    request_command(command);
    networks_update();
}

void send_connect(const network_row &row) {
    char command[command_bytes];
    build_command(command, "connect", row.ssid_hex, nullptr);
    set_pending(row);
    wchar_t line[96];
    wsprintfW(line, L"connect %s", row.ssid);
    append_log(line);
    request_command(command);
    networks_update();
}

void send_forget(const network_row &row) {
    wchar_t question[128];
    wsprintfW(question, L"Forget %s? Brainwave will need its password again to join it.", row.ssid);
    if (MessageBoxW(main_window, question, L"Forget Network", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    char command[command_bytes];
    build_command(command, "forget", row.ssid_hex, nullptr);
    wchar_t line[96];
    wsprintfW(line, L"forget %s", row.ssid);
    append_log(line);
    request_command(command);
    request_known();
}

void send_disconnect() {
    append_log(L"disconnect");
    model.pending_ssid[0] = 0;
    model.action_note[0] = 0;
    request_command("disconnect");
}

void ask_password_and_join(const network_row &row) {
    wchar_t password[72] = L"";
    if (!password_dialog(main_window, row.ssid, password, 72)) {
        return;
    }
    char password_hex[160];
    bool encoded = wide_to_hex(password, password_hex, sizeof(password_hex));
    memset(password, 0, sizeof(password));
    if (encoded) {
        send_join(row, password_hex);
    }
    memset(password_hex, 0, sizeof(password_hex));
}

void activate(int index) {
    if (index < 0 || !model.status.reachable) {
        return;
    }
    network_row row = rows[index];
    if (row.connected || !row.in_range) {
        return;
    }
    if (row.saved) {
        send_connect(row);
    } else if (!is_secured(row.security)) {
        send_join(row, "-");
    } else {
        ask_password_and_join(row);
    }
}

void show_context_menu(int index) {
    if (index < 0 || !model.status.reachable) {
        return;
    }
    network_row row = rows[index];
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | ((row.connected || !row.in_range) ? MF_GRAYED : 0), menu_connect_id, L"Connect");
    AppendMenuW(menu, MF_STRING | ((is_secured(row.security) && row.in_range) ? 0 : MF_GRAYED), menu_password_id, L"Enter Password...");
    AppendMenuW(menu, MF_STRING | (row.saved ? 0 : MF_GRAYED), menu_forget_id, L"Forget");
    POINT point;
    GetCursorPos(&point);
    UINT command = TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, point.x, point.y, panel, nullptr);
    DestroyMenu(menu);
    if (command == menu_connect_id) {
        activate(index);
    } else if (command == menu_password_id) {
        ask_password_and_join(row);
    } else if (command == menu_forget_id) {
        send_forget(row);
    }
}

void size_columns(int width) {
    int signal_width = measure_text_width(list, L"Signal") + style.margin * 4;
    int security_width = measure_text_width(list, L"Security") + style.margin * 4;
    int status_width = measure_text_width(list, L"Saved, not in range") + style.margin * 4;
    int network_width = width - signal_width - security_width - status_width - GetSystemMetrics(SM_CXVSCROLL) - 4;
    int minimum = style.list_icon_size + measure_text_width(list, L"Network name");
    if (network_width < minimum) {
        network_width = minimum;
    }
    SendMessageW(list, LVM_SETCOLUMNWIDTH, 0, network_width);
    SendMessageW(list, LVM_SETCOLUMNWIDTH, 1, signal_width);
    SendMessageW(list, LVM_SETCOLUMNWIDTH, 2, security_width);
    SendMessageW(list, LVM_SETCOLUMNWIDTH, 3, status_width);
}

void layout(int width, int height) {
    int left = style.margin;
    int inner = width - style.margin * 2;
    int top = style.margin;
    MoveWindow(state_title, left, top, inner, style.line_height, TRUE);
    top += style.line_height;
    MoveWindow(state_hint, left, top, inner, style.line_height, TRUE);
    top += style.line_height + style.margin / 2;
    int buttons_top = height - style.margin - style.button_height;
    MoveWindow(list, left, top, inner, buttons_top - style.margin - top, TRUE);
    int refresh_width = button_width_for(refresh_button, scanning_label);
    MoveWindow(refresh_button, width - style.margin - refresh_width, buttons_top, refresh_width, style.button_height, TRUE);
    int connect_width = button_width_for(connect_button, connect_label);
    int forget_width = button_width_for(forget_button, forget_label);
    MoveWindow(connect_button, left, buttons_top, connect_width, style.button_height, TRUE);
    MoveWindow(forget_button, left + connect_width + style.margin, buttons_top, forget_width, style.button_height, TRUE);
    int disconnect_width = button_width_for(disconnect_button, disconnect_label);
    MoveWindow(disconnect_button, left + connect_width + forget_width + style.margin * 2, buttons_top, disconnect_width, style.button_height,
               TRUE);
    size_columns(inner);
}

void add_image(int size, int bars, bool secured, bool dimmed) {
    HBITMAP colour = create_signal_bitmap(size, bars, secured, dimmed, false);
    HBITMAP mask = create_signal_bitmap(size, bars, secured, dimmed, true);
    ImageList_Add(images, colour, mask);
    DeleteObject(colour);
    DeleteObject(mask);
}

void create_images() {
    int size = style.list_icon_size;
    images = ImageList_Create(size, size, ILC_COLOR | ILC_MASK, image_dimmed + 1, 0);
    for (int secured = 0; secured < 2; secured++) {
        for (int bars = 0; bars <= 4; bars++) {
            add_image(size, bars, secured != 0, false);
        }
    }
    add_image(size, 0, false, true);
}

LRESULT CALLBACK networks_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_NOTIFY: {
        NMHDR *header = reinterpret_cast<NMHDR *>(lparam);
        if (header->hwndFrom != list) {
            break;
        }
        if (header->code == NM_DBLCLK || header->code == NM_RETURN) {
            activate(selected_index());
        } else if (header->code == NM_RCLICK) {
            show_context_menu(selected_index());
        } else if (header->code == LVN_ITEMCHANGED) {
            update_buttons();
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case connect_button_id:
            activate(selected_index());
            break;
        case forget_button_id: {
            int selected = selected_index();
            if (selected >= 0 && rows[selected].saved) {
                send_forget(rows[selected]);
            }
            break;
        }
        case refresh_button_id:
            networks_start_scan();
            break;
        case disconnect_button_id:
            send_disconnect();
            break;
        }
        return 0;
    case WM_TIMER:
        if (wparam == auto_scan_timer && IsWindowVisible(window) && IsWindowVisible(main_window) && model.status.reachable &&
            !model.scanning) {
            networks_start_scan();
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

HWND networks_create(HWND parent) {
    WNDCLASSW description = {};
    description.lpfnWndProc = networks_procedure;
    description.hInstance = program_instance;
    description.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    description.lpszClassName = networks_class;
    RegisterClassW(&description);
    panel = CreateWindowExW(0, networks_class, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr,
                            program_instance, nullptr);
    state_title = create_control(panel, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    SendMessageW(state_title, WM_SETFONT, reinterpret_cast<WPARAM>(style.bold_font), FALSE);
    state_hint = create_control(panel, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0);
    list = create_control(panel, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, list_id);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    create_images();
    SendMessageW(list, LVM_SETIMAGELIST, LVSIL_SMALL, reinterpret_cast<LPARAM>(images));
    for (int column = 0; column < column_count; column++) {
        LVCOLUMNW column_description = {};
        column_description.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column_description.pszText = const_cast<wchar_t *>(column_titles[column]);
        column_description.cx = 80;
        column_description.iSubItem = column;
        SendMessageW(list, LVM_INSERTCOLUMNW, column, reinterpret_cast<LPARAM>(&column_description));
    }
    connect_button = create_control(panel, L"BUTTON", connect_label, BS_PUSHBUTTON | WS_TABSTOP, connect_button_id);
    forget_button = create_control(panel, L"BUTTON", forget_label, BS_PUSHBUTTON | WS_TABSTOP, forget_button_id);
    refresh_button = create_control(panel, L"BUTTON", refresh_label, BS_PUSHBUTTON | WS_TABSTOP, refresh_button_id);
    disconnect_button = create_control(panel, L"BUTTON", disconnect_label, BS_PUSHBUTTON | WS_TABSTOP, disconnect_button_id);
    SetTimer(panel, auto_scan_timer, auto_scan_milliseconds, nullptr);
    send_group_boxes_to_back(panel);
    return panel;
}

void networks_update() {
    build_rows();
    reconcile_list();
    wchar_t title[128];
    wchar_t hint[128];
    state_text(title, hint);
    set_text_if_changed(state_title, title);
    set_text_if_changed(state_hint, hint);
    update_buttons();
}

void networks_status_text(wchar_t *text, int characters) {
    wchar_t title[128];
    wchar_t hint[128];
    state_text(title, hint);
    copy_wide(text, title, characters);
}

void networks_start_scan() {
    if (model.scanning) {
        return;
    }
    model.scanning = true;
    request_scan();
    networks_update();
}

void networks_shown(bool) {
}
