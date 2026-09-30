#include "pcibackend.h"

#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>

#include <string>
#include <vector>

namespace {

constexpr wchar_t kDriverPath[] = L"\\\\.\\PciConfigReader";

constexpr DWORD kIoctlReadConfig =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS);

struct ReadRequest {
    ULONG bus;
    ULONG device;
    ULONG function;
    ULONG offset;
    ULONG length;
};

struct ReadResponse {
    ULONG length;
    UCHAR data[256];
};

QString registryString(HDEVINFO devInfo, PSP_DEVINFO_DATA data, DWORD prop)
{
    DWORD required = 0;
    DWORD type = 0;
    SetupDiGetDeviceRegistryPropertyW(devInfo, data, prop, &type, nullptr, 0, &required);
    if (required == 0)
        return QString();

    std::vector<wchar_t> buffer(required / sizeof(wchar_t) + 1, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(devInfo, data, prop, &type,
                                           reinterpret_cast<PBYTE>(buffer.data()),
                                           required, nullptr))
        return QString();

    return QString::fromWCharArray(buffer.data());
}

bool registryDword(HDEVINFO devInfo, PSP_DEVINFO_DATA data, DWORD prop, DWORD &value)
{
    DWORD type = 0;
    return SetupDiGetDeviceRegistryPropertyW(devInfo, data, prop, &type,
                                             reinterpret_cast<PBYTE>(&value),
                                             sizeof(value), nullptr) != FALSE;
}

HANDLE openDriverDevice()
{
    return CreateFileW(kDriverPath, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool loadDriverService(QString &error)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        error = QStringLiteral("Не удалось открыть Service Control Manager (нужны права администратора)");
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, L"pcidriver", SERVICE_ALL_ACCESS);
    if (!service) {
        service = CreateServiceW(scm, L"pcidriver", L"PCI Config Reader",
                                 SERVICE_ALL_ACCESS,
                                 SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START,
                                 SERVICE_ERROR_NORMAL,
                                 L"\\SystemRoot\\System32\\drivers\\pcidriver.sys",
                                 nullptr, nullptr, nullptr, nullptr, nullptr);
    }

    CloseServiceHandle(scm);

    if (!service) {
        error = QStringLiteral("Не удалось создать сервис драйвера. Установите его: "
                               "запустите driver\\setup_driver.bat от имени администратора.");
        return false;
    }

    SERVICE_STATUS status = {};
    if (QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_RUNNING) {
        CloseServiceHandle(service);
        return true;
    }

    if (!StartServiceW(service, 0, nullptr)) {
        const DWORD err = GetLastError();
        CloseServiceHandle(service);
        if (err == ERROR_SERVICE_ALREADY_RUNNING)
            return true;
        error = QStringLiteral("Не удалось запустить драйвер (код %1).").arg(err);
        return false;
    }

    CloseServiceHandle(service);
    return true;
}

} // namespace

QHash<quint32, QString> pciDeviceDescriptions()
{
    QHash<quint32, QString> map;

    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_SYSTEM, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE)
        return map;

    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &info); ++i) {
        const QString enumerator = registryString(devInfo, &info, SPDRP_ENUMERATOR_NAME);
        if (enumerator.compare(QStringLiteral("PCI"), Qt::CaseInsensitive) != 0)
            continue;

        DWORD bus = 0;
        DWORD address = 0;
        registryDword(devInfo, &info, SPDRP_BUSNUMBER, bus);
        registryDword(devInfo, &info, SPDRP_ADDRESS, address);

        const quint32 device = (address >> 16) & 0xFFFF;
        const quint32 function = address & 0xFFFF;
        const QString desc = registryString(devInfo, &info, SPDRP_DEVICEDESC);
        const quint32 key = (bus << 16) | (device << 8) | function;
        map.insert(key, desc);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return map;
}

PciDeviceHandle openPciDevice(QString &error)
{
    HANDLE h = openDriverDevice();
    if (h == INVALID_HANDLE_VALUE) {
        QString loadError;
        if (loadDriverService(loadError))
            h = openDriverDevice();
        if (h == INVALID_HANDLE_VALUE) {
            error = QStringLiteral("Устройство \\\\.\\PciConfigReader недоступно. %1").arg(loadError);
            return nullptr;
        }
    }
    return reinterpret_cast<PciDeviceHandle>(h);
}

void closePciDevice(PciDeviceHandle handle)
{
    if (handle)
        CloseHandle(reinterpret_cast<HANDLE>(handle));
}

bool readConfigSpace(PciDeviceHandle handle, quint32 bus, quint32 device, quint32 function,
                     QByteArray &data, QString &error)
{
    data.clear();

    HANDLE h = reinterpret_cast<HANDLE>(handle);
    if (!h || h == INVALID_HANDLE_VALUE) {
        error = QStringLiteral("Драйвер не открыт.");
        return false;
    }

    ReadRequest req = {};
    req.bus = bus;
    req.device = device;
    req.function = function;
    req.offset = 0;
    req.length = 256;

    ReadResponse resp = {};
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, kIoctlReadConfig, &req, sizeof(req),
                                    &resp, sizeof(resp), &returned, nullptr);
    const DWORD ioErr = GetLastError();

    if (!ok || resp.length == 0) {
        error = QStringLiteral("Драйвер вернул 0 байт (код %1).").arg(ioErr);
        return false;
    }

    const ULONG read = resp.length > 256 ? 256 : resp.length;
    data = QByteArray(reinterpret_cast<const char *>(resp.data), static_cast<int>(read));
    return true;
}

QVector<PciFunction> scanPciBus(PciDeviceHandle handle, quint32 bus,
                                const QHash<quint32, QString> &descriptions, QString &error)
{
    QVector<PciFunction> result;

    for (quint32 device = 0; device < 32; ++device) {
        QByteArray cfg0;
        QString e;
        if (!readConfigSpace(handle, bus, device, 0, cfg0, e))
            continue;

        const quint16 vendor0 = static_cast<quint16>(static_cast<uchar>(cfg0[0]))
            | (static_cast<quint16>(static_cast<uchar>(cfg0[1])) << 8);
        if (vendor0 == 0xFFFF || vendor0 == 0x0000)
            continue;

        const bool multifunction = (static_cast<uchar>(cfg0[0x0E]) & 0x80) != 0;

        QVector<quint32> functions{0};
        if (multifunction) {
            for (quint32 f = 1; f < 8; ++f) {
                QByteArray cfgF;
                QString e2;
                if (!readConfigSpace(handle, bus, device, f, cfgF, e2))
                    continue;
                const quint16 vf = static_cast<quint16>(static_cast<uchar>(cfgF[0]))
                    | (static_cast<quint16>(static_cast<uchar>(cfgF[1])) << 8);
                if (vf == 0xFFFF || vf == 0x0000)
                    continue;
                functions.append(f);
            }
        }

        for (const quint32 function : functions) {
            QByteArray cfg;
            QString e3;
            if (!readConfigSpace(handle, bus, device, function, cfg, e3))
                continue;

            if (cfg.size() < 0x40)
                continue;

            PciFunction fn;
            fn.bus = bus;
            fn.device = device;
            fn.function = function;
            fn.config = cfg;
            fn.vendorId = static_cast<quint16>(static_cast<uchar>(cfg[0]))
                | (static_cast<quint16>(static_cast<uchar>(cfg[1])) << 8);
            fn.deviceId = static_cast<quint16>(static_cast<uchar>(cfg[2]))
                | (static_cast<quint16>(static_cast<uchar>(cfg[3])) << 8);
            fn.revision = static_cast<quint8>(cfg[0x08]);
            fn.progIf = static_cast<quint8>(cfg[0x09]);
            fn.subClass = static_cast<quint8>(cfg[0x0A]);
            fn.baseClass = static_cast<quint8>(cfg[0x0B]);
            fn.headerType = static_cast<quint8>(cfg[0x0E]) & 0x7F;
            fn.isBridge = (fn.baseClass == 0x06 && fn.subClass == 0x04);
            if (fn.isBridge)
                fn.secondaryBus = static_cast<quint8>(cfg[0x19]);

            const quint32 key = (bus << 16) | (device << 8) | function;
            fn.deviceDescription = descriptions.value(key);

            result.append(fn);
        }
    }

    return result;
}