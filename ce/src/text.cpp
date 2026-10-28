// Conversions between the protocol's hex and ASCII strings and the UI's UTF-16 text.
#include "text.h"

namespace {

int hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

}

bool hex_to_wide(const char *hex, wchar_t *wide, int wide_characters) {
    char bytes[128];
    int length = 0;
    wide[0] = 0;
    if (hex[0] == '-' && hex[1] == 0) {
        return true;
    }
    while (hex[0] && hex[1]) {
        int high = hex_digit(hex[0]);
        int low = hex_digit(hex[1]);
        if (high < 0 || low < 0 || length >= static_cast<int>(sizeof(bytes))) {
            return false;
        }
        bytes[length++] = static_cast<char>(high << 4 | low);
        hex += 2;
    }
    if (hex[0]) {
        return false;
    }
    int converted = length ? MultiByteToWideChar(CP_UTF8, 0, bytes, length, wide, wide_characters - 1) : 0;
    if (length && converted <= 0) {
        for (converted = 0; converted < length && converted + 1 < wide_characters; converted++) {
            wide[converted] = static_cast<unsigned char>(bytes[converted]);
        }
    }
    wide[converted] = 0;
    return true;
}

bool wide_to_hex(const wchar_t *wide, char *hex, int hex_bytes) {
    static const char digits[] = "0123456789abcdef";
    char bytes[256];
    if (!wide[0]) {
        if (hex_bytes < 2) {
            return false;
        }
        hex[0] = '-';
        hex[1] = 0;
        return true;
    }
    int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, bytes, sizeof(bytes), nullptr, nullptr);
    if (length <= 1 || (length - 1) * 2 + 1 > hex_bytes) {
        return false;
    }
    for (int index = 0; index < length - 1; index++) {
        unsigned char value = static_cast<unsigned char>(bytes[index]);
        hex[index * 2] = digits[value >> 4];
        hex[index * 2 + 1] = digits[value & 15];
    }
    hex[(length - 1) * 2] = 0;
    return true;
}

void ascii_to_wide(const char *ascii, wchar_t *wide, int wide_characters) {
    int index = 0;
    for (; index + 1 < wide_characters && ascii[index]; index++) {
        wide[index] = static_cast<unsigned char>(ascii[index]);
    }
    wide[index] = 0;
}

void wide_to_ascii(const wchar_t *wide, char *ascii, int ascii_bytes) {
    int index = 0;
    for (; index + 1 < ascii_bytes && wide[index]; index++) {
        ascii[index] = wide[index] < 128 ? static_cast<char>(wide[index]) : '?';
    }
    ascii[index] = 0;
}

bool starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) {
            return false;
        }
    }
    return true;
}
