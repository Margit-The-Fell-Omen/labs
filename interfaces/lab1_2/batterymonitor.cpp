#include "batterymonitor.h"

#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <batclass.h>

#include <cstdlib>

BatteryMonitor::BatteryMonitor(QObject *parent)
    : QObject(parent)
{
}

QString BatteryMonitor::statusText() const
{
    if (!m_available)
        return QStringLiteral("Батарея не обнаружена");
    QString s = QStringLiteral("Заряд: %1%").arg(m_chargeLevel);
    s += m_acOnline ? QStringLiteral(" · от сети")
                    : QStringLiteral(" · от батареи");
    return s;
}

void BatteryMonitor::refresh()
{
    HDEVINFO hdev = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hdev == INVALID_HANDLE_VALUE) {
        m_available = false;
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

    BATTERY_STATUS status = {};
    BATTERY_WAIT_STATUS wait = {};
    wait.BatteryTag = tag;
    DWORD bytes = 0;
    if (DeviceIoControl(hBattery, IOCTL_BATTERY_QUERY_STATUS, &wait, sizeof(wait),
                        &status, sizeof(status), &bytes, nullptr)) {
        m_chargeLevel = static_cast<int>(status.Capacity);
        m_acOnline = (status.PowerState & BATTERY_POWER_ON_LINE) != 0;
        m_available = true;
    }

    CloseHandle(hBattery);
    emit changed();
}
