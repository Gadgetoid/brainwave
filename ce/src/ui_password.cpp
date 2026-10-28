// The modal network password dialog, built from an in-memory template.
#include "ui_password.h"

#include "app.h"
#include "ui_style.h"

namespace {

constexpr int prompt_id = 601;
constexpr int edit_id = 602;
constexpr int show_id = 603;
constexpr int template_words = 512;
constexpr int minimum_password = 8;
constexpr int maximum_password = 63;
constexpr WORD class_button = 0x0080;
constexpr WORD class_edit = 0x0081;
constexpr WORD class_static = 0x0082;

constexpr const wchar_t *dialog_title = L"Network Password";
constexpr const wchar_t *show_label = L"Show characters";
constexpr const wchar_t *ok_label = L"OK";
constexpr const wchar_t *cancel_label = L"Cancel";

struct dialog_request {
    const wchar_t *network;
    bool wep;
    wchar_t *password;
    int password_characters;
    wchar_t prompt[128];
};

bool valid_wpa_password(const wchar_t *password) {
    int length = lstrlenW(password);
    return length >= minimum_password && length <= maximum_password;
}

bool valid_wep_key(const wchar_t *key) {
    int length = lstrlenW(key);
    if (length == 5 || length == 13) {
        return true;
    }
    if (length != 10 && length != 26) {
        return false;
    }
    for (int index = 0; index < length; index++) {
        wchar_t character = key[index];
        bool hex = (character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f') || (character >= L'A' && character <= L'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

class template_writer {
public:
    explicit template_writer(WORD *buffer) : start(buffer), cursor(buffer) {}

    void word(WORD value) { *cursor++ = value; }

    void dword(DWORD value) {
        word(static_cast<WORD>(value & 0xffff));
        word(static_cast<WORD>(value >> 16));
    }

    void text(const wchar_t *value) {
        do {
            word(static_cast<WORD>(*value));
        } while (*value++);
    }

    void align() {
        if ((cursor - start) & 1) {
            word(0);
        }
    }

    void item(DWORD item_style, WORD id, WORD control_class, const wchar_t *title) {
        align();
        dword(WS_CHILD | WS_VISIBLE | item_style);
        dword(0);
        word(0);
        word(0);
        word(10);
        word(10);
        word(id);
        word(0xffff);
        word(control_class);
        text(title);
        word(0);
    }

    DLGTEMPLATE *result() const { return reinterpret_cast<DLGTEMPLATE *>(start); }

private:
    WORD *start;
    WORD *cursor;
};

void layout(HWND dialog) {
    int margin = style.margin * 2;
    int width = style.cell_height * 22;
    HWND prompt = GetDlgItem(dialog, prompt_id);
    wchar_t prompt_text[128];
    GetWindowTextW(prompt, prompt_text, 128);
    int prompt_height = measure_wrapped_height(prompt, prompt_text, width);
    int top = margin;
    MoveWindow(prompt, margin, top, width, prompt_height, FALSE);
    top += prompt_height + style.margin;
    MoveWindow(GetDlgItem(dialog, edit_id), margin, top, width, style.button_height, FALSE);
    top += style.button_height + style.margin;
    int show_height = check_height_for(GetDlgItem(dialog, show_id), show_label, width);
    MoveWindow(GetDlgItem(dialog, show_id), margin, top, width, show_height, FALSE);
    top += show_height + style.margin * 2;
    HWND ok_button = GetDlgItem(dialog, IDOK);
    HWND cancel_button = GetDlgItem(dialog, IDCANCEL);
    int ok_width = button_width_for(ok_button, cancel_label);
    MoveWindow(cancel_button, margin + width - ok_width, top, ok_width, style.button_height, FALSE);
    MoveWindow(ok_button, margin + width - ok_width * 2 - style.margin, top, ok_width, style.button_height, FALSE);
    top += style.button_height + margin;
    RECT frame = { 0, 0, width + margin * 2, top };
    AdjustWindowRectEx(&frame, GetWindowLongW(dialog, GWL_STYLE), FALSE, GetWindowLongW(dialog, GWL_EXSTYLE));
    int frame_width = frame.right - frame.left;
    int frame_height = frame.bottom - frame.top;
    RECT owner = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    HWND parent = GetParent(dialog);
    if (parent) {
        GetWindowRect(parent, &owner);
    }
    int left = owner.left + (owner.right - owner.left - frame_width) / 2;
    int window_top = owner.top + (owner.bottom - owner.top - frame_height) / 3;
    MoveWindow(dialog, left < 0 ? 0 : left, window_top < 0 ? 0 : window_top, frame_width, frame_height, TRUE);
}

BOOL CALLBACK dialog_procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    dialog_request *request = reinterpret_cast<dialog_request *>(GetWindowLongW(dialog, DWL_USER));
    switch (message) {
    case WM_INITDIALOG: {
        request = reinterpret_cast<dialog_request *>(lparam);
        SetWindowLongW(dialog, DWL_USER, static_cast<LONG>(lparam));
        const int ids[] = { prompt_id, edit_id, show_id, IDOK, IDCANCEL };
        for (int id : ids) {
            SendMessageW(GetDlgItem(dialog, id), WM_SETFONT, reinterpret_cast<WPARAM>(style.font), FALSE);
        }
        SetWindowTextW(GetDlgItem(dialog, prompt_id), request->prompt);
        SendMessageW(GetDlgItem(dialog, edit_id), EM_LIMITTEXT, maximum_password, 0);
        layout(dialog);
        SetFocus(GetDlgItem(dialog, edit_id));
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case show_id: {
            bool visible = SendMessageW(GetDlgItem(dialog, show_id), BM_GETCHECK, 0, 0) == BST_CHECKED;
            HWND edit = GetDlgItem(dialog, edit_id);
            SendMessageW(edit, EM_SETPASSWORDCHAR, visible ? 0 : L'*', 0);
            InvalidateRect(edit, nullptr, TRUE);
            SetFocus(edit);
            return TRUE;
        }
        case IDOK: {
            GetWindowTextW(GetDlgItem(dialog, edit_id), request->password, request->password_characters);
            if (!(request->wep ? valid_wep_key(request->password) : valid_wpa_password(request->password))) {
                MessageBoxW(dialog, request->wep ? L"WEP keys are 5 or 13 characters, or 10 or 26 hexadecimal digits." : L"WPA passwords are 8 to 63 characters long.",
                            dialog_title, MB_OK | MB_ICONEXCLAMATION);
                SetFocus(GetDlgItem(dialog, edit_id));
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

}

bool password_dialog(HWND owner, const wchar_t *network, bool wep, wchar_t *password, int password_characters) {
    static WORD buffer[template_words];
    template_writer writer(buffer);
    writer.dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME);
    writer.dword(0);
    writer.word(5);
    writer.word(0);
    writer.word(0);
    writer.word(100);
    writer.word(60);
    writer.word(0);
    writer.word(0);
    writer.text(dialog_title);
    writer.item(SS_LEFT | SS_NOPREFIX, prompt_id, class_static, L"");
    writer.item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, edit_id, class_edit, L"");
    writer.item(WS_TABSTOP | BS_AUTOCHECKBOX, show_id, class_button, show_label);
    writer.item(WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, class_button, ok_label);
    writer.item(WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, class_button, cancel_label);

    dialog_request request = {};
    request.network = network;
    request.wep = wep;
    request.password = password;
    request.password_characters = password_characters;
    wsprintfW(request.prompt, L"Enter the password for \"%s\". Brainwave remembers it for next time.", network);
    INT_PTR result = DialogBoxIndirectParamW(program_instance, writer.result(), owner, dialog_procedure, reinterpret_cast<LPARAM>(&request));
    if (result != IDOK) {
        password[0] = 0;
        return false;
    }
    return true;
}
