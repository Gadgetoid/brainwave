// Conversions between the protocol's hex and ASCII strings and the UI's UTF-16 text.
#pragma once

#include <windows.h>

bool hex_to_wide(const char *hex, wchar_t *wide, int wide_characters);
bool wide_to_hex(const wchar_t *wide, char *hex, int hex_bytes);
void ascii_to_wide(const char *ascii, wchar_t *wide, int wide_characters);
void wide_to_ascii(const wchar_t *wide, char *ascii, int ascii_bytes);
bool starts_with(const char *text, const char *prefix);
