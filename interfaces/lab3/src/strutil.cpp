#include "strutil.h"

#include <cstdarg>
#include <cwchar>

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::wstring widen(const char* utf8)
{
    return widen(std::string(utf8 ? utf8 : ""));
}

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return std::string();
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n,
                        nullptr, nullptr);
    return out;
}

std::wstring wformat(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list args;
    va_start(args, fmt);
    int n = vswprintf(buf, 2048, fmt, args);
    va_end(args);
    if (n < 0)
        return std::wstring();
    return std::wstring(buf, static_cast<size_t>(n));
}