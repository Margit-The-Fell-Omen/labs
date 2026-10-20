#include "winapi.h"

#include "strutil.h"

#include <windows.h>
#include <winioctl.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kMaxPhysicalDrives = 32;

constexpr STORAGE_PROPERTY_ID kStorageDeviceTemperatureProperty =
    static_cast<STORAGE_PROPERTY_ID>(StorageDeviceProtocolSpecificProperty + 2);

class ScopedHandle
{
public:
    explicit ScopedHandle(HANDLE h) : m_handle(h) {}
    ~ScopedHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

private:
    HANDLE m_handle;
};

bool queryStorageProperty(HANDLE handle, STORAGE_PROPERTY_ID id, DWORD versionSize,
                          std::vector<unsigned char>& out)
{
    STORAGE_PROPERTY_QUERY query = {};
    query.PropertyId = id;
    query.QueryType = PropertyStandardQuery;

    out.resize(4096);
    std::memset(out.data(), 0, out.size());
    *reinterpret_cast<DWORD*>(out.data()) = versionSize;

    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                         out.data(), static_cast<DWORD>(out.size()), &returned, nullptr))
        return false;

    if (returned == 0)
        return false;

    out.resize(returned);
    return true;
}

// STORAGE_DEVICE_DESCRIPTOR stores strings as "<u16 length><bytes>" runs at
// fixed offsets, without guaranteed null termination.
std::wstring readDescriptorString(const std::vector<unsigned char>& buf, DWORD offset)
{
    if (offset == 0)
        return std::wstring();

    const int pos = static_cast<int>(offset);
    if (pos < 0 || pos + 2 > static_cast<int>(buf.size()))
        return std::wstring();

    const unsigned char* p = buf.data() + pos;
    const int len = static_cast<int>(p[0] | (static_cast<unsigned short>(p[1]) << 8));
    const int strStart = pos + 2;
    if (len <= 0 || strStart + len > static_cast<int>(buf.size()))
        return std::wstring();

    std::string s(reinterpret_cast<const char*>(buf.data()) + strStart, len);
    return widen(s);
}

bool readDeviceSize(HANDLE handle, unsigned long long& sizeBytes)
{
    GET_LENGTH_INFORMATION lengthInfo = {};
    DWORD returned = 0;
    if (!DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                         &lengthInfo, sizeof(lengthInfo), &returned, nullptr))
        return false;
    sizeBytes = static_cast<unsigned long long>(lengthInfo.Length.QuadPart);
    return true;
}

} // namespace

std::vector<StorageDevice> WinApiEnumerator::enumerate()
{
    std::vector<StorageDevice> result;

    for (int i = 0; i < kMaxPhysicalDrives; ++i) {
        const std::wstring path = wformat(L"\\\\.\\PhysicalDrive%d", i);

        HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        ScopedHandle handle(h);
        StorageDevice device;
        device.diskNumber = i;
        device.detectedByWinApi = true;

        std::vector<unsigned char> deviceDescriptor;
        if (queryStorageProperty(h, StorageDeviceProperty, sizeof(STORAGE_DEVICE_DESCRIPTOR),
                                 deviceDescriptor)
            && deviceDescriptor.size() >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(deviceDescriptor.data());
            device.busType = static_cast<int>(desc->BusType);
            device.busTypeName = busTypeToString(device.busType);

            const std::wstring vendor = readDescriptorString(deviceDescriptor, desc->VendorIdOffset);
            const std::wstring product = readDescriptorString(deviceDescriptor, desc->ProductIdOffset);

            if (!vendor.empty() && !product.empty())
                device.model = vendor + L" " + product;
            else if (!product.empty())
                device.model = product;
            else
                device.model = vendor;

            device.friendlyName = product.empty() ? vendor : product;
            device.serialNumber = readDescriptorString(deviceDescriptor, desc->SerialNumberOffset);
        }

        std::vector<unsigned char> seekDescriptor;
        if (queryStorageProperty(h, StorageDeviceSeekPenaltyProperty,
                                 sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR), seekDescriptor)
            && seekDescriptor.size() >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR)) {
            auto* desc = reinterpret_cast<DEVICE_SEEK_PENALTY_DESCRIPTOR*>(seekDescriptor.data());
            device.seekPenaltyKnown = true;
            device.seekPenalty = (desc->IncursSeekPenalty != FALSE);
        }

        std::vector<unsigned char> trimDescriptor;
        if (queryStorageProperty(h, StorageDeviceTrimProperty,
                                 sizeof(DEVICE_TRIM_DESCRIPTOR), trimDescriptor)
            && trimDescriptor.size() >= sizeof(DEVICE_TRIM_DESCRIPTOR)) {
            auto* desc = reinterpret_cast<DEVICE_TRIM_DESCRIPTOR*>(trimDescriptor.data());
            device.trimKnown = true;
            device.trimSupported = (desc->TrimEnabled != FALSE);
        }

        std::vector<unsigned char> tempDescriptor;
        if (queryStorageProperty(h, kStorageDeviceTemperatureProperty,
                                 sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR), tempDescriptor)
            && tempDescriptor.size() >= sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) {
            auto* desc = reinterpret_cast<STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(tempDescriptor.data());
            if (desc->InfoCount > 0) {
                device.hasTemperature = true;
                device.temperatureC = static_cast<int>(desc->TemperatureInfo[0].Temperature);
            }
        }

        unsigned long long sizeBytes = 0;
        if (readDeviceSize(h, sizeBytes))
            device.sizeBytes = sizeBytes;

        result.push_back(std::move(device));
    }

    return result;
}
