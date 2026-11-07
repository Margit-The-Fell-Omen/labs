#pragma once

#include <string>
#include <windows.h>

std::wstring widen(const char* utf8);
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
std::wstring wformat(const wchar_t* fmt, ...);