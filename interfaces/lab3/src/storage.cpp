#include "storage.h"

#include "strutil.h"

std::wstring StorageDevice::displayName() const
{
    std::wstring label = wformat(L"PhysicalDrive%d", diskNumber);
    if (!friendlyName.empty())
        label += wformat(L" — %ls", friendlyName.c_str());
    else if (!model.empty())
        label += wformat(L" — %ls", model.c_str());
    return label;
}

std::wstring driveTypeToString(DriveType type)
{
    switch (type) {
    case DriveType::HDD:
        return widen("HDD");
    case DriveType::SSD:
        return widen("SSD");
    case DriveType::Unknown:
    default:
        return widen("Не определено");
    }
}

std::wstring busTypeToString(int busType)
{
    switch (busType) {
    case 0: return widen("Неизвестно");
    case 1: return widen("SCSI");
    case 2: return widen("ATAPI");
    case 3: return widen("ATA");
    case 4: return widen("IEEE 1394");
    case 5: return widen("SSA");
    case 6: return widen("Fibre Channel");
    case 7: return widen("USB");
    case 8: return widen("RAID");
    case 9: return widen("iSCSI");
    case 10: return widen("SAS");
    case 11: return widen("SATA");
    case 12: return widen("SD");
    case 13: return widen("MMC");
    case 14: return widen("Virtual");
    case 15: return widen("File Backed Virtual");
    case 16: return widen("Storage Spaces");
    case 17: return widen("NVMe");
    case 18: return widen("SCM");
    default: return wformat(L"Другое (%d)", busType);
    }
}

std::wstring formatBytes(unsigned long long bytes)
{
    if (bytes == 0)
        return widen("N/A");
    const double d = static_cast<double>(bytes);
    if (d >= 1e12)
        return wformat(L"%.2f ТБ", d / 1e12);
    if (d >= 1e9)
        return wformat(L"%.2f ГБ", d / 1e9);
    if (d >= 1e6)
        return wformat(L"%.1f МБ", d / 1e6);
    return wformat(L"%u КБ", static_cast<unsigned int>(bytes / 1000));
}

std::wstring formatHours(unsigned long long hours)
{
    if (hours == 0)
        return widen("N/A");
    const unsigned long long days = hours / 24;
    const unsigned long long years = days / 365;
    if (years > 0)
        return wformat(L"%u лет %u дн", static_cast<unsigned int>(years),
                       static_cast<unsigned int>(days % 365));
    if (days > 0)
        return wformat(L"%u дн (%u ч)", static_cast<unsigned int>(days),
                       static_cast<unsigned int>(hours));
    return wformat(L"%u ч", static_cast<unsigned int>(hours));
}

std::wstring formatRpm(unsigned int rpm, bool known)
{
    if (!known || rpm == 0)
        return widen("N/A");
    return wformat(L"%u об/мин", rpm);
}