#include "report.h"

#include "strutil.h"

#include <windows.h>

#include <string>

namespace {

std::string esc(const std::wstring& ws)
{
    std::string s = narrow(ws);
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string levelColor(Advice::Level level)
{
    switch (level) {
    case Advice::Critical:
        return "#b91c1c";
    case Advice::Warning:
        return "#b45309";
    default:
        return "#1d4ed8";
    }
}

std::string buildHtml(const StorageDevice& left,
                      const StorageDevice& right,
                      const std::vector<CompareRow>& rows,
                      const std::vector<Advice>& advices)
{
    std::string h;
    h += "<!doctype html><html><head><meta charset=\"utf-8\">"
         "<title>Сравнительный отчёт HDD / SSD</title>"
         "<style>body{font-family:'Segoe UI',Arial,sans-serif;margin:24px;color:#111}"
         "table{border-collapse:collapse;width:100%}"
         "td,th{border:1px solid #ccc;padding:6px 10px;text-align:left}"
         "th{background:#f0f0f0}ul{padding-left:18px}</style></head><body>";

    h += "<h1>Сравнительный отчёт накопителей HDD / SSD</h1>";

    std::string leftName = esc(left.displayName());
    std::string rightName = esc(right.displayName());

    h += "<h2>" + leftName + " — " + esc(driveTypeToString(left.type)) + "</h2>";
    h += "<ul><li><b>Модель:</b> " + esc(left.model) + "</li>"
         "<li><b>Серийный номер:</b> " + esc(left.serialNumber) + "</li>"
         "<li><b>Объём:</b> " + esc(formatBytes(left.sizeBytes)) + "</li></ul>";

    h += "<h2>" + rightName + " — " + esc(driveTypeToString(right.type)) + "</h2>";
    h += "<ul><li><b>Модель:</b> " + esc(right.model) + "</li>"
         "<li><b>Серийный номер:</b> " + esc(right.serialNumber) + "</li>"
         "<li><b>Объём:</b> " + esc(formatBytes(right.sizeBytes)) + "</li></ul>";

    h += "<h2>Сравнение параметров</h2><table><tr><th>Параметр</th><th>" + leftName
         + "</th><th>" + rightName + "</th></tr>";
    for (const CompareRow& r : rows) {
        h += "<tr><td><b>" + esc(r.label) + "</b></td><td>" + esc(r.left)
             + "</td><td>" + esc(r.right) + "</td></tr>";
    }
    h += "</table>";

    if (!advices.empty()) {
        h += "<h2>Рекомендации</h2><ul>";
        for (const Advice& a : advices) {
            const std::string scope = a.diskNumber < 0
                                          ? "Сводно"
                                          : ("PhysicalDrive" + std::to_string(a.diskNumber));
            h += "<li style=\"color:" + levelColor(a.level) + ";\">[" + scope + "] "
                 + esc(a.text) + "</li>";
        }
        h += "</ul>";
    }

    h += "</body></html>";
    return h;
}

} // namespace

bool exportReportHtml(const std::wstring& filePath,
                      const StorageDevice& left,
                      const StorageDevice& right,
                      const std::vector<CompareRow>& rows,
                      const std::vector<Advice>& advices,
                      std::wstring& errorMessage)
{
    const std::string html = buildHtml(left, right, rows, advices);

    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        errorMessage = wformat(L"Не удалось создать файл отчёта: %ls", filePath.c_str());
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file, html.data(), static_cast<DWORD>(html.size()), &written, nullptr);
    CloseHandle(file);

    if (!ok || written != html.size()) {
        errorMessage = widen("Ошибка при записи отчёта.");
        return false;
    }

    errorMessage = wformat(L"Отчёт сохранён: %ls", filePath.c_str());
    return true;
}