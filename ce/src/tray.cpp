// The notification area icon: signal bars drawn at runtime, crossed out when WiFi or the module is unavailable.
#include "tray.h"

#include <shellapi.h>

#include "app.h"
#include "ui_style.h"

namespace {

constexpr UINT tray_icon_id = 1;
constexpr int icon_size = 16;

struct ce_tray_icon_data {
    DWORD size;
    HWND window;
    UINT id;
    UINT flags;
    UINT callback_message;
    HICON icon;
    wchar_t tip[64];
};

HICON current_icon = nullptr;
bool added = false;
int shown_key = -1;
wchar_t shown_tip[64] = L"";

void draw_icon_bars(HDC dc, int bars, bool reachable, bool connected, bool mask) {
    COLORREF on = mask ? RGB(0, 0, 0) : (connected ? RGB(255, 255, 255) : RGB(150, 150, 150));
    COLORREF off = mask ? RGB(0, 0, 0) : RGB(90, 90, 90);
    for (int index = 0; index < 4; index++) {
        int height = 4 + index * 3;
        RECT bar = { 1 + index * 4, icon_size - 1 - height, 1 + index * 4 + 3, icon_size - 1 };
        fill_rectangle(dc, bar, index < bars ? on : off);
    }
    if (!connected) {
        HPEN pen = CreatePen(PS_SOLID, 2, mask ? RGB(0, 0, 0) : (reachable ? RGB(230, 160, 40) : RGB(220, 60, 50)));
        HGDIOBJ previous = SelectObject(dc, pen);
        MoveToEx(dc, 9, 1, nullptr);
        LineTo(dc, 15, 7);
        MoveToEx(dc, 15, 1, nullptr);
        LineTo(dc, 9, 7);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
}

HICON create_icon(int bars, bool reachable, bool connected) {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP colour = CreateCompatibleBitmap(screen, icon_size, icon_size);
    HBITMAP mask = CreateBitmap(icon_size, icon_size, 1, 1, nullptr);
    HGDIOBJ previous = SelectObject(dc, mask);
    PatBlt(dc, 0, 0, icon_size, icon_size, WHITENESS);
    draw_icon_bars(dc, bars, reachable, connected, true);
    SelectObject(dc, colour);
    PatBlt(dc, 0, 0, icon_size, icon_size, BLACKNESS);
    draw_icon_bars(dc, bars, reachable, connected, false);
    SelectObject(dc, previous);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = colour;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(colour);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}

ce_tray_icon_data icon_data(HWND window) {
    ce_tray_icon_data data = {};
    data.size = sizeof(data);
    data.window = window;
    data.id = tray_icon_id;
    data.flags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.callback_message = tray_message;
    data.icon = current_icon;
    lstrcpynW(data.tip, shown_tip, 64);
    return data;
}

}

void tray_update(HWND window, bool reachable, bool connected, int bars, const wchar_t *tip) {
    int key = (reachable ? 100 : 0) + (connected ? 10 : 0) + (connected ? bars : 0);
    if (added && key == shown_key && !lstrcmpW(tip, shown_tip)) {
        return;
    }
    lstrcpynW(shown_tip, tip, 64);
    HICON previous = current_icon;
    if (key != shown_key || !current_icon) {
        current_icon = create_icon(connected ? bars : 0, reachable, connected);
        shown_key = key;
    }
    ce_tray_icon_data data = icon_data(window);
    if (!added) {
        added = Shell_NotifyIcon(NIM_ADD, reinterpret_cast<PNOTIFYICONDATAW>(&data)) != FALSE;
        if (!added) {
            append_log(L"tray icon unavailable");
        }
    } else {
        Shell_NotifyIcon(NIM_MODIFY, reinterpret_cast<PNOTIFYICONDATAW>(&data));
    }
    if (previous && previous != current_icon) {
        DestroyIcon(previous);
    }
}

void tray_remove(HWND window) {
    if (!added) {
        return;
    }
    ce_tray_icon_data data = icon_data(window);
    Shell_NotifyIcon(NIM_DELETE, reinterpret_cast<PNOTIFYICONDATAW>(&data));
    added = false;
}
