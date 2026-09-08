#include "batteryinfo.h"

#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <batclass.h>

#include <cstdlib>
#include <cstring>

namespace {

QString chemistryName(const QString &code)
{
    const QString c = code.trimmed().toUpper();
    if (c.contains("LION") || c.startsWith("LI-I") || c == "LII" ||
        c == "LIION" || c == "LI-ION")
        return QStringLiteral("Li-Ion");
    if (c.startsWith("LIP") || c.contains("LIPO") || c.contains("LI-P") ||
        c.contains("LIPOL"))
        return QStringLiteral("Li-Po");
    if (c.contains("NIMH"))
        return QStringLiteral("Ni-MH");
    if (c.contains("NICD"))
        return QStringLiteral("Ni-Cd");
    if (c.contains("NIZN"))
        return QStringLiteral("Ni-Zn");
    if (c.contains("LIFE"))
        return QStringLiteral("LiFePO4");
    if (c.contains("LIMN"))
        return QStringLiteral("Li-Mn");
    if (c.contains("PBAC") || c.contains("LEAD"))
        return QStringLiteral("Lead-Acid");
    if (c.contains("ZNAI") || c.contains("ZINC"))
        return QStringLiteral("Zinc-Air");
    if (c.isEmpty())
        return QStringLiteral("Unknown");
    return code.trimmed();
}

} // namespace

BatteryInfo::BatteryInfo(QObject *parent)
    : QObject(parent)
{
}

void BatteryInfo::refresh()
{
    HDEVINFO hdev = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdev == INVALID_HANDLE_VALUE) {
        emit changed();
        return;
    }

    HANDLE hBattery = INVALID_HANDLE_VALUE;
    ULONG tag = 0;

    SP_DEVICE_INTERFACE_DATA did = {};
    did.cbSize = sizeof(did);
    if (SetupDiEnumDeviceInterfaces(hdev, nullptr, &GUID_DEVCLASS_BATTERY, 0, &did)) {
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetail(hdev, &did, nullptr, 0, &required, nullptr);
        PSP_DEVICE_INTERFACE_DETAIL_DATA detail =
            static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(malloc(required));
        if (detail) {
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            if (SetupDiGetDeviceInterfaceDetail(hdev, &did, detail, required, nullptr, nullptr)) {
                hBattery = CreateFile(detail->DevicePath,
                                      GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                free(detail);
                if (hBattery != INVALID_HANDLE_VALUE) {
                    DWORD bytes = 0;
                    if (!DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_TAG, nullptr, 0,
                                         &tag, sizeof(tag), &bytes, nullptr))
                        tag = 0;
                }
            } else {
                free(detail);
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hdev);

    if (hBattery == INVALID_HANDLE_VALUE) {
        m_available = false;
        emit changed();
        return;
    }

    DWORD bytes = 0;

    BATTERY_INFORMATION info = {};
    BATTERY_QUERY_INFORMATION q = {};
    q.BatteryTag = tag;
    q.InformationLevel = BatteryInformation;
    if (DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_INFORMATION, &q, sizeof(q),
                        &info, sizeof(info), &bytes, nullptr)) {
        m_chemistry = chemistryName(QString::fromLatin1(
            reinterpret_cast<const char *>(info.Chemistry), 4));
        m_designCapacity = static_cast<int>(info.DesignedCapacity);
        m_fullChargeCapacity = static_cast<int>(info.FullChargedCapacity);
        m_cycleCount = static_cast<int>(info.CycleCount);
        m_available = true;
    }

    ULONG tempTenthsKelvin = 0;
    q.InformationLevel = BatteryTemperature;
    if (DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_INFORMATION, &q, sizeof(q),
                        &tempTenthsKelvin, sizeof(tempTenthsKelvin), &bytes, nullptr)) {
        m_temperature = static_cast<int>(tempTenthsKelvin / 10.0 - 273.15);
    } else {
        m_temperature = 0;
    }

    BATTERY_STATUS status = {};
    BATTERY_WAIT_STATUS wait = {};
    wait.BatteryTag = tag;
    if (DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_STATUS, &wait, sizeof(wait),
                        &status, sizeof(status), &bytes, nullptr)) {
        m_currentCapacity = static_cast<int>(status.Capacity);
        m_voltage = static_cast<int>(status.Voltage);
        m_acOnline = (status.PowerState & BATTERY_POWER_ON_LINE) != 0;
        m_available = true;
    }

    CloseHandle(hBattery);

    if (m_designCapacity > 0) {
        m_wearPercent = (m_designCapacity - m_fullChargeCapacity) * 100.0 / m_designCapacity;
        if (m_wearPercent < 0.0)
            m_wearPercent = 0.0;
    } else {
        m_wearPercent = 0.0;
    }

    if (m_fullChargeCapacity > 0)
        m_chargeLevel = m_currentCapacity * 100.0 / m_fullChargeCapacity;
    else if (m_designCapacity > 0)
        m_chargeLevel = m_currentCapacity * 100.0 / m_designCapacity;
    else
        m_chargeLevel = 0.0;

    if (m_chargeLevel < 0.0)
        m_chargeLevel = 0.0;
    if (m_chargeLevel > 100.0)
        m_chargeLevel = 100.0;

    emit changed();
}
