// Fonts, metrics and drawing helpers shared by the tabs.
#pragma once

#include <windows.h>

struct ui_style {
    HFONT font;
    HFONT bold_font;
    int cell_height;
    int line_height;
    int margin;
    int button_height;
    int list_icon_size;
};

extern ui_style style;

void style_create(int screen_height);
int signal_bars(int rssi);
int signal_percent(int rssi);
void fill_rectangle(HDC dc, const RECT &area, COLORREF colour);
int measure_text_width(HWND window, const wchar_t *text);
int measure_wrapped_height(HWND window, const wchar_t *text, int width);
int button_width_for(HWND window, const wchar_t *text);
int check_height_for(HWND window, const wchar_t *text, int width);
HWND create_control(HWND parent, const wchar_t *control_class, const wchar_t *text, DWORD control_style, int id);
void set_text_if_changed(HWND control, const wchar_t *text);
HBITMAP create_signal_bitmap(int size, int bars, bool secured, bool dimmed, bool mask);

class back_buffer {
public:
    back_buffer(HWND window, HDC target);
    ~back_buffer();
    HDC dc() const { return memory; }
    const RECT &area() const { return client; }

private:
    HDC target_dc;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    RECT client;
};

void send_group_boxes_to_back(HWND panel);
