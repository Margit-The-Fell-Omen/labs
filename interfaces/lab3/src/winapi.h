#pragma once

#include "storage.h"

#include <vector>

// Enumerates physical disks through the Win32 DeviceIoControl interface,
// using IOCTL_STORAGE_QUERY_PROPERTY to read:
//   - StorageDeviceSeekPenaltyProperty  (seek penalty -> HDD/SSD)
//   - StorageDeviceTrimProperty         (TRIM support -> SSD)
//   - StorageDeviceProperty             (bus type, serial, model, ...)
//   - StorageDeviceTemperatureProperty  (current temperature)
// and IOCTL_DISK_GET_LENGTH_INFO for the raw device size.
class WinApiEnumerator
{
public:
    static std::vector<StorageDevice> enumerate();
};