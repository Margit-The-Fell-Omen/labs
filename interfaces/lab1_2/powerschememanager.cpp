#include "powerschememanager.h"

#include <powrprof.h>

#include <cstring>

extern "C" {
DWORD WINAPI PowerGetActiveScheme(HKEY UserRootPowerKey, GUID **ActivePolicyGuid);
DWORD WINAPI PowerSetActiveScheme(HKEY UserRootPowerKey, const GUID *SchemeGuid);
}

namespace {

const GUID kVideoSubgroup =
    { 0x7516B95F, 0xF776, 0x4464, { 0x8C, 0x53, 0x06, 0x16, 0x7F, 0x40, 0xCC, 0x99 } };
const GUID kVideoIdle =
    { 0x3C0BC021, 0xC8A8, 0x4E07, { 0xA9, 0x73, 0x6B, 0x14, 0xCB, 0xCB, 0x2B, 0x7E } };
const GUID kSleepSubgroup =
    { 0x238C9FA8, 0x0AAD, 0x41ED, { 0x83, 0xF4, 0x97, 0xBE, 0x24, 0x2C, 0x8F, 0x20 } };
const GUID kStandbyIdle =
    { 0x29F6C1DB, 0x86DA, 0x48C5, { 0x9F, 0xDB, 0xF2, 0xB6, 0x7B, 0x1F, 0x44, 0xDA } };
const GUID kBatterySubgroup =
    { 0xE73A048D, 0xBF27, 0x4F12, { 0x97, 0x31, 0x8B, 0x20, 0x76, 0xE8, 0x89, 0x1F } };
const GUID kCriticalBatteryLevel =
    { 0x9A66D8D7, 0x4FF7, 0x4EF9, { 0xB5, 0xA2, 0x5A, 0x32, 0x6C, 0xA2, 0xA4, 0x69 } };

QString readFriendlyName(const GUID &scheme)
{
    wchar_t buffer[256] = {};
    DWORD size = sizeof(buffer);
    if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr,
                              reinterpret_cast<UCHAR *>(buffer), &size) == ERROR_SUCCESS)
        return QString::fromWCharArray(buffer);
    return QString();
}

} // namespace

PowerSchemeManager::PowerSchemeManager(QObject *parent)
    : QObject(parent)
{
}

QStringList PowerSchemeManager::schemes() const
{
    QStringList names;
    names.reserve(static_cast<int>(m_schemes.size()));
    for (const auto &s : m_schemes)
        names << s.name;
    return names;
}

QString PowerSchemeManager::selectedName() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_schemes.size()))
        return m_schemes[m_selectedIndex].name;
    return QString();
}

void PowerSchemeManager::refresh()
{
    m_schemes.clear();

    UCHAR buffer[sizeof(GUID)];
    DWORD size = sizeof(buffer);
    ULONG index = 0;
    while (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                          buffer, &size) == ERROR_SUCCESS) {
        GUID guid;
        std::memcpy(&guid, buffer, sizeof(GUID));
        Scheme s;
        s.guid = guid;
        s.name = readFriendlyName(guid);
        if (s.name.isEmpty())
            s.name = QStringLiteral("Схема ") + QString::number(index + 1);
        m_schemes.push_back(s);
        ++index;
        size = sizeof(buffer);
    }

    m_activeIndex = -1;
    GUID *active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) == ERROR_SUCCESS && active) {
        for (size_t i = 0; i < m_schemes.size(); ++i) {
            if (IsEqualGUID(m_schemes[i].guid, *active)) {
                m_activeIndex = static_cast<int>(i);
                break;
            }
        }
        LocalFree(active);
    }

    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_schemes.size()))
        m_selectedIndex = m_activeIndex;

    if (m_selectedIndex >= 0)
        loadParameters(m_selectedIndex);

    emit changed();
}

void PowerSchemeManager::select(int index)
{
    if (index < 0 || index >= static_cast<int>(m_schemes.size()))
        return;
    if (index == m_selectedIndex)
        return;
    m_selectedIndex = index;
    loadParameters(index);
    emit changed();
}

void PowerSchemeManager::activate(int index)
{
    if (index < 0 || index >= static_cast<int>(m_schemes.size()))
        return;
    if (PowerSetActiveScheme(nullptr, &m_schemes[index].guid) == ERROR_SUCCESS) {
        m_activeIndex = index;
        emit changed();
    }
}

void PowerSchemeManager::loadParameters(int index)
{
    const GUID &scheme = m_schemes[index].guid;

    DWORD v = 0;
    if (PowerReadACValueIndex(nullptr, &scheme, &kVideoSubgroup, &kVideoIdle, &v) == ERROR_SUCCESS)
        m_displayAC = static_cast<int>(v);
    else
        m_displayAC = -1;

    v = 0;
    if (PowerReadDCValueIndex(nullptr, &scheme, &kVideoSubgroup, &kVideoIdle, &v) == ERROR_SUCCESS)
        m_displayDC = static_cast<int>(v);
    else
        m_displayDC = -1;

    v = 0;
    if (PowerReadACValueIndex(nullptr, &scheme, &kSleepSubgroup, &kStandbyIdle, &v) == ERROR_SUCCESS)
        m_sleepAC = static_cast<int>(v);
    else
        m_sleepAC = -1;

    v = 0;
    if (PowerReadDCValueIndex(nullptr, &scheme, &kSleepSubgroup, &kStandbyIdle, &v) == ERROR_SUCCESS)
        m_sleepDC = static_cast<int>(v);
    else
        m_sleepDC = -1;

    v = 0;
    if (PowerReadACValueIndex(nullptr, &scheme, &kBatterySubgroup, &kCriticalBatteryLevel, &v) == ERROR_SUCCESS)
        m_criticalAC = static_cast<int>(v);
    else
        m_criticalAC = -1;

    v = 0;
    if (PowerReadDCValueIndex(nullptr, &scheme, &kBatterySubgroup, &kCriticalBatteryLevel, &v) == ERROR_SUCCESS)
        m_criticalDC = static_cast<int>(v);
    else
        m_criticalDC = -1;
}
