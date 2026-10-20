#pragma once

#include <string>
#include <windows.h>

// UTF-8 <-> UTF-16 conversions. Source files are UTF-8; Win32 APIs use wide
// strings, so all user-facing strings pass through widen()/narrow().
std::wstring widen(const char* utf8);
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);

// printf-style wide string formatting (wraps vswprintf).
std::wstring wformat(const wchar_t* fmt, ...);