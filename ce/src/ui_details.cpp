// The Details tab: wireless, TCP/IP and module values, and a graph of signal strength over the last five minutes.
#include "ui_details.h"

#include <string.h>

#include "app.h"
#include "model.h"
#include "text.h"
#include "ui_style.h"

namespace {

constexpr const wchar_t *details_class = L"BrainwaveDetails";
constexpr const wchar_t *graph_class = L"BrainwaveSignalGraph";
constexpr int field_count = 14;
constexpr int field_characters = 48;
constexpr int group_count = 4;
constexpr int graph_top_dbm = -30;
constexpr int graph_bottom_dbm = -95;

struct field_group {
    const wchar_t *title;
    int first_field;
    int field_total;
};

const wchar_t *field_labels[field_count] = {
    L"Network:", L"Signal:", L"Channel:", L"Security:", L"Access point:", L"IP address:", L"Subnet mask:",
    L"Gateway:", L"DNS server:", L"Brain address:", L"MAC address:", L"Battery:", L"Uptime:", L"Firmware:",
};

const field_group groups[group_count] = {
    { L"Wireless", 0, 5 },
    { L"TCP/IP", 5, 5 },
    { L"Module", 10, 4 },
    { L"Signal strength, last 5 minutes", 0, 0 },
};

HWND panel = nullptr;
HWND graph = nullptr;
HWND group_boxes[group_count] = {};
HWND labels[field_count] = {};
HWND values[field_count] = {};
int shown_history_count = -1;
int shown_history_start = -1;

void set_ascii(wchar_t *value, const char *text) {
    if (!text[0] || !strcmp(text, "-")) {
        lstrcpyW(value, L"-");
        return;
    }
    ascii_to_wide(text, value, field_characters);
}

void upper_case(wchar_t *value) {
    for (; *value; value++) {
        if (*value >= L'a' && *value <= L'z') {
            *value = static_cast<wchar_t>(*value - L'a' + L'A');
        }
    }
}

void compute_values(wchar_t computed[field_count][field_characters]) {
    const module_status &status = model.status;
    bool reachable = status.reachable;
    bool connected = model_connected();
    bool has_network = reachable && (connected || !strcmp(status.wifi, "connecting") || !strcmp(status.wifi, "failed"));
    for (int index = 0; index < field_count; index++) {
        lstrcpyW(computed[index], L"-");
    }
    if (has_network && status.ssid[0]) {
        lstrcpynW(computed[0], status.ssid, field_characters);
    }
    if (connected) {
        int rssi = model_rssi();
        wsprintfW(computed[1], L"%d dBm (%d%%)", rssi, signal_percent(rssi));
    }
    if (has_network) {
        set_ascii(computed[2], status.channel);
        set_ascii(computed[3], status.security);
        upper_case(computed[3]);
    }
    if (connected) {
        set_ascii(computed[4], status.bssid);
        set_ascii(computed[5], status.ip);
        set_ascii(computed[6], status.netmask);
        set_ascii(computed[7], status.gateway);
        set_ascii(computed[8], status.dns);
    }
    if (reachable) {
        set_ascii(computed[9], status.brain);
        set_ascii(computed[10], status.mac);
        if (status.battery_millivolts > 0) {
            wsprintfW(computed[11], L"%d.%02d V", status.battery_millivolts / 1000, (status.battery_millivolts % 1000) / 10);
        }
        if (status.has_uptime) {
            int seconds = status.uptime_seconds;
            if (seconds >= 3600) {
                wsprintfW(computed[12], L"%d h %02d min", seconds / 3600, (seconds / 60) % 60);
            } else {
                wsprintfW(computed[12], L"%d min %02d s", seconds / 60, seconds % 60);
            }
        }
        set_ascii(computed[13], status.version);
    }
}

int graph_y(const RECT &area, int rssi) {
    if (rssi > graph_top_dbm) {
        rssi = graph_top_dbm;
    }
    if (rssi < graph_bottom_dbm) {
        rssi = graph_bottom_dbm;
    }
    return area.top + (graph_top_dbm - rssi) * (area.bottom - area.top - 1) / (graph_top_dbm - graph_bottom_dbm);
}

void paint_graph(HDC target) {
    back_buffer buffer(graph, target);
    HDC dc = buffer.dc();
    RECT client = buffer.area();
    fill_rectangle(dc, client, GetSysColor(COLOR_WINDOW));
    DrawEdge(dc, &client, EDGE_SUNKEN, BF_RECT);
    RECT area = { client.left + 2, client.top + 2, client.right - 2, client.bottom - 2 };
    HGDIOBJ previous_font = SelectObject(dc, style.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    const int guides[] = { -40, -60, -80 };
    for (int guide : guides) {
        int y = graph_y(area, guide);
        RECT line = { area.left, y, area.right, y + 1 };
        fill_rectangle(dc, line, GetSysColor(COLOR_BTNSHADOW));
        wchar_t label[16];
        wsprintfW(label, L"%d dBm", guide);
        RECT label_area = { area.left + style.margin / 2, y - style.cell_height, area.right, y };
        DrawTextW(dc, label, -1, &label_area, DT_LEFT | DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX);
    }
    if (model.history_count < 2) {
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(dc, model_connected() ? L"Collecting..." : L"Not connected", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous_font);
        return;
    }
    SelectObject(dc, previous_font);
    HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_HIGHLIGHT));
    HGDIOBJ previous = SelectObject(dc, pen);
    POINT points[history_limit];
    int point_count = 0;
    int width = area.right - area.left - 1;
    for (int index = 0; index <= model.history_count; index++) {
        int rssi = index < model.history_count ? model_history_at(index) : history_gap;
        if (rssi == history_gap) {
            if (point_count > 1) {
                Polyline(dc, points, point_count);
            }
            point_count = 0;
            continue;
        }
        int slot = history_limit - model.history_count + index;
        points[point_count].x = area.left + slot * width / (history_limit - 1);
        points[point_count].y = graph_y(area, rssi);
        point_count++;
    }
    SelectObject(dc, previous);
    DeleteObject(pen);
}

LRESULT CALLBACK graph_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint_info;
        HDC dc = BeginPaint(window, &paint_info);
        paint_graph(dc);
        EndPaint(window, &paint_info);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int widest_label() {
    int widest = 0;
    for (int index = 0; index < field_count; index++) {
        int width = measure_text_width(panel, field_labels[index]);
        if (width > widest) {
            widest = width;
        }
    }
    return widest;
}

int place_group(int group, int left, int top, int width) {
    const field_group &description = groups[group];
    int title_space = style.line_height;
    int inner_left = left + style.margin * 2;
    int label_width = widest_label() + style.margin;
    int row_top = top + title_space;
    for (int field = description.first_field; field < description.first_field + description.field_total; field++) {
        MoveWindow(labels[field], inner_left, row_top, label_width, style.line_height, TRUE);
        MoveWindow(values[field], inner_left + label_width, row_top, left + width - style.margin * 2 - inner_left - label_width, style.line_height,
                   TRUE);
        row_top += style.line_height;
    }
    int height = title_space + description.field_total * style.line_height + style.margin;
    MoveWindow(group_boxes[group], left, top, width, height, TRUE);
    return top + height;
}

void layout(int width, int height) {
    int gap = style.margin;
    int column_width = (width - gap * 3) / 2;
    int left_column = gap;
    int right_column = gap * 2 + column_width;
    int top = gap / 2;
    int left_bottom = place_group(0, left_column, top, column_width);
    left_bottom = place_group(2, left_column, left_bottom + gap / 2, column_width);
    int right_bottom = place_group(1, right_column, top, column_width);
    int graph_group_top = right_bottom + gap / 2;
    int graph_group_bottom = height - gap / 2;
    MoveWindow(group_boxes[3], right_column, graph_group_top, column_width, graph_group_bottom - graph_group_top, TRUE);
    MoveWindow(graph, right_column + style.margin * 2, graph_group_top + style.line_height, column_width - style.margin * 4,
               graph_group_bottom - graph_group_top - style.line_height - style.margin, TRUE);
}

LRESULT CALLBACK details_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SIZE) {
        layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

HWND details_create(HWND parent) {
    WNDCLASSW description = {};
    description.lpfnWndProc = details_procedure;
    description.hInstance = program_instance;
    description.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    description.lpszClassName = details_class;
    RegisterClassW(&description);
    WNDCLASSW graph_description = {};
    graph_description.lpfnWndProc = graph_procedure;
    graph_description.hInstance = program_instance;
    graph_description.lpszClassName = graph_class;
    RegisterClassW(&graph_description);
    panel = CreateWindowExW(0, details_class, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent, nullptr, program_instance, nullptr);
    for (int group = 0; group < group_count; group++) {
        group_boxes[group] = create_control(panel, L"BUTTON", groups[group].title, BS_GROUPBOX, 0);
    }
    for (int field = 0; field < field_count; field++) {
        labels[field] = create_control(panel, L"STATIC", field_labels[field], SS_LEFT | SS_NOPREFIX, 0);
        values[field] = create_control(panel, L"STATIC", L"-", SS_LEFT | SS_NOPREFIX, 0);
    }
    graph = CreateWindowExW(0, graph_class, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, panel, nullptr, program_instance, nullptr);
    send_group_boxes_to_back(panel);
    return panel;
}

void details_update() {
    wchar_t computed[field_count][field_characters];
    compute_values(computed);
    for (int field = 0; field < field_count; field++) {
        set_text_if_changed(values[field], computed[field]);
    }
    if (model.history_count != shown_history_count || model.history_start != shown_history_start) {
        shown_history_count = model.history_count;
        shown_history_start = model.history_start;
        InvalidateRect(graph, nullptr, FALSE);
    }
}
