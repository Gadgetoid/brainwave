// Fonts, metrics and the drawing helpers shared by the tabs, based on the system font and colours.
#include "ui_style.h"

#include "app.h"

ui_style style = {};

namespace {

int font_cell_height(HFONT font) {
    HDC dc = GetDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, font);
    TEXTMETRICW metrics = {};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(nullptr, dc);
    return metrics.tmHeight;
}

HFONT choose_font(int screen_height, LOGFONTW &chosen) {
    HFONT system_font = static_cast<HFONT>(GetStockObject(SYSTEM_FONT));
    GetObjectW(system_font, sizeof(chosen), &chosen);
    int cell = font_cell_height(system_font);
    int readable = screen_height / 28;
    wchar_t line[128];
    wsprintfW(line, L"system font %s, height %d, cell %d px, readable minimum %d px", chosen.lfFaceName, static_cast<int>(chosen.lfHeight),
              cell, readable);
    append_log(line);
    if (cell >= readable) {
        return system_font;
    }
    chosen.lfHeight = -readable * 4 / 5;
    chosen.lfWidth = 0;
    HFONT scaled = CreateFontIndirectW(&chosen);
    wsprintfW(line, L"scaled system font to cell %d px", font_cell_height(scaled));
    append_log(line);
    return scaled;
}

}

void style_create(int screen_height) {
    LOGFONTW chosen = {};
    style.font = choose_font(screen_height, chosen);
    chosen.lfWeight = FW_BOLD;
    style.bold_font = CreateFontIndirectW(&chosen);
    int cell = font_cell_height(style.font);
    style.cell_height = cell;
    style.line_height = cell + cell / 3;
    style.margin = cell / 2;
    style.button_height = cell + cell * 3 / 4;
    int icon = cell * 2;
    style.list_icon_size = icon < 24 ? 24 : (icon > 40 ? 40 : icon);
}

int signal_bars(int rssi) {
    if (rssi >= -55) {
        return 4;
    }
    if (rssi >= -65) {
        return 3;
    }
    if (rssi >= -75) {
        return 2;
    }
    if (rssi >= -85) {
        return 1;
    }
    return 0;
}

int signal_percent(int rssi) {
    int percent = (rssi + 90) * 100 / 60;
    if (percent < 0) {
        return 0;
    }
    return percent > 100 ? 100 : percent;
}

void fill_rectangle(HDC dc, const RECT &area, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(dc, &area, brush);
    DeleteObject(brush);
}

int measure_text_width(HWND window, const wchar_t *text) {
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, style.font);
    RECT measured = { 0, 0, 0, 0 };
    DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return measured.right - measured.left;
}

int measure_wrapped_height(HWND window, const wchar_t *text, int width) {
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, style.font);
    RECT measured = { 0, 0, width, 0 };
    DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return measured.bottom - measured.top;
}

int button_width_for(HWND window, const wchar_t *text) {
    int width = measure_text_width(window, text) + style.margin * 4;
    int minimum = style.button_height * 3;
    return width < minimum ? minimum : width;
}

int check_height_for(HWND window, const wchar_t *text, int width) {
    int box = style.cell_height + style.margin;
    int height = measure_wrapped_height(window, text, width - box) + style.margin;
    return height < style.line_height ? style.line_height : height;
}

HWND create_control(HWND parent, const wchar_t *control_class, const wchar_t *text, DWORD control_style, int id) {
    HWND control = CreateWindowExW(0, control_class, text, WS_CHILD | WS_VISIBLE | control_style, 0, 0, 10, 10, parent,
                                   reinterpret_cast<HMENU>(id), program_instance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(style.font), FALSE);
    return control;
}

void set_text_if_changed(HWND control, const wchar_t *text) {
    wchar_t current[256];
    GetWindowTextW(control, current, 256);
    if (lstrcmpW(current, text)) {
        SetWindowTextW(control, text);
    }
}

HBITMAP create_signal_bitmap(int size, int bars, bool secured, bool dimmed, bool mask) {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = mask ? CreateBitmap(size, size, 1, 1, nullptr) : CreateCompatibleBitmap(screen, size, size);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    PatBlt(dc, 0, 0, size, size, mask ? WHITENESS : BLACKNESS);
    COLORREF on = mask ? RGB(0, 0, 0) : (dimmed ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_HIGHLIGHT));
    COLORREF off = mask ? RGB(0, 0, 0) : GetSysColor(COLOR_BTNSHADOW);
    int area = secured ? size * 3 / 4 : size;
    int bar_width = area / 6;
    int gap = (area - bar_width * 4) / 4;
    int bottom = size - size / 8;
    for (int index = 0; index < 4; index++) {
        int height = (bottom - size / 8) * (index + 1) / 4;
        int left = gap / 2 + index * (bar_width + gap);
        RECT bar = { left, bottom - height, left + bar_width, bottom };
        fill_rectangle(dc, bar, index < bars ? on : off);
    }
    if (secured) {
        int lock = size / 3;
        int left = size - lock - 1;
        int body_top = bottom - lock * 2 / 3;
        COLORREF ink = mask ? RGB(0, 0, 0) : GetSysColor(COLOR_WINDOWTEXT);
        RECT body = { left, body_top, left + lock, bottom };
        fill_rectangle(dc, body, ink);
        int shackle = lock / 4 + 1;
        RECT shackle_left = { left + lock / 6, body_top - lock / 2, left + lock / 6 + shackle / 2 + 1, body_top };
        RECT shackle_right = { left + lock - lock / 6 - shackle / 2 - 1, body_top - lock / 2, left + lock - lock / 6, body_top };
        RECT shackle_top = { left + lock / 6, body_top - lock / 2, left + lock - lock / 6, body_top - lock / 2 + shackle / 2 + 1 };
        fill_rectangle(dc, shackle_left, ink);
        fill_rectangle(dc, shackle_right, ink);
        fill_rectangle(dc, shackle_top, ink);
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

back_buffer::back_buffer(HWND window, HDC target) : target_dc(target) {
    GetClientRect(window, &client);
    memory = CreateCompatibleDC(target);
    bitmap = CreateCompatibleBitmap(target, client.right > 0 ? client.right : 1, client.bottom > 0 ? client.bottom : 1);
    previous = SelectObject(memory, bitmap);
}

back_buffer::~back_buffer() {
    BitBlt(target_dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

void send_group_boxes_to_back(HWND panel) {
    HWND group_boxes[16];
    int count = 0;
    for (HWND child = GetWindow(panel, GW_CHILD); child && count < 16; child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t class_name[16] = L"";
        GetClassNameW(child, class_name, 16);
        if (!lstrcmpiW(class_name, L"BUTTON") && (GetWindowLongW(child, GWL_STYLE) & 0x0f) == BS_GROUPBOX) {
            group_boxes[count++] = child;
        }
    }
    for (int index = 0; index < count; index++) {
        SetWindowPos(group_boxes[index], HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
