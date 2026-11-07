#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct LogEntry
{
    SYSTEMTIME time;
    std::wstring text;
};

class ReportLog
{
public:
    void add(const std::wstring& text);
    bool save(const std::wstring& path) const;
    const std::vector<LogEntry>& entries() const { return m_entries; }
    void clear();

private:
    std::vector<LogEntry> m_entries;
};