#include "reportlog.h"

#include "strutil.h"

#include <cwchar>

namespace {

constexpr size_t kMaxEntries = 3000;

void timestampToLine(const SYSTEMTIME& t, wchar_t* buf, size_t size)
{
    swprintf(buf, size, L"%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond);
}

} // namespace

void ReportLog::add(const std::wstring& text)
{
    LogEntry entry;
    GetLocalTime(&entry.time);
    entry.text = text;
    m_entries.push_back(std::move(entry));
    if (m_entries.size() > kMaxEntries)
        m_entries.erase(m_entries.begin(), m_entries.end() - 1000);
}

void ReportLog::clear()
{
    m_entries.clear();
}

bool ReportLog::save(const std::wstring& path) const
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    WriteFile(h, bom, 3, &written, nullptr);

    bool ok = true;
    for (const LogEntry& entry : m_entries) {
        wchar_t ts[64];
        timestampToLine(entry.time, ts, 64);
        std::wstring line = wformat(L"%ls — %ls\r\n", ts, entry.text.c_str());
        std::string utf8 = narrow(line);
        DWORD n = static_cast<DWORD>(utf8.size());
        WriteFile(h, utf8.c_str(), n, &written, nullptr);
        if (written != n) {
            ok = false;
            break;
        }
    }

    CloseHandle(h);
    return ok;
}