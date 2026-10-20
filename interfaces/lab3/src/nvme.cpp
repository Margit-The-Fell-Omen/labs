#include "nvme.h"

#include "strutil.h"

#include <windows.h>
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <cstdint>

namespace {

// ---- Windows SDK types absent from MinGW (ABI-stable) ----

enum AppProtocolType {
    appProtocolNvme = 3, // STORAGE_PROTOCOL_TYPE: Nvme
};

enum AppNvmeDataType {
    appNvmeDataTypeUnknown = 0,
    appNvmeDataTypeIdentify = 1,
    appNvmeDataTypeLogPage = 2,
    appNvmeDataTypeFeature = 3,
    appNvmeDataTypeLogPageEx = 4,
};

struct AppStorageProtocolSpecificDataExt {
    DWORD ProtocolType;   // STORAGE_PROTOCOL_TYPE
    DWORD DataType;       // NVMeDataType*
    DWORD ProtocolDataRequestValue;  // CNS / log page id / feature id
    DWORD ProtocolDataRequestSubValue; // NSID, sub-arg
    DWORD ProtocolDataOffset;
    DWORD ProtocolDataLength;
    DWORD FixedProtocolReturnData;
    DWORD ProtocolDataRequestSubValue2;
    DWORD ProtocolDataRequestSubValue3;
    DWORD ProtocolDataRequestSubValue4;
};
static_assert(sizeof(AppStorageProtocolSpecificDataExt) == 40, "struct size");

// ---- NVMe payload views (byte offsets per spec) ----

constexpr DWORD kIdentifySize = 4096;
constexpr DWORD kSmartSize = 512;

// Identify Controller: MN=4..44, SN=68..88, FR=92..100, NN=236 (16-bit).
// Identify Namespace: NSZE=0 (128-bit, low 64 used), FLBAS=26, LBAF start=128.
// SMART/Health: Temp(K)=0, PercentageUsed=4, PowerCycles=112(64),
//              PowerOnHours=128(64), UnsafeShutdowns=144(64), MediaErrors=152(64).

DWORD le32(const unsigned char* p)
{
    return static_cast<DWORD>(p[0]) | (static_cast<DWORD>(p[1]) << 8)
        | (static_cast<DWORD>(p[2]) << 16) | (static_cast<DWORD>(p[3]) << 24);
}

unsigned long long le64(const unsigned char* p)
{
    unsigned long long v = 0;
    for (int i = 0; i < 8; ++i)
        v |= (static_cast<unsigned long long>(p[i]) << (8 * i));
    return v;
}

std::string ascii(const unsigned char* p, size_t len)
{
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = p[i];
        s += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : ' ';
    }
    return s;
}

bool openDisk(int diskNumber, HANDLE& handle, const wchar_t* access, DWORD& error)
{
    const std::wstring path = wformat(L"\\\\.\\PhysicalDrive%d", diskNumber);
    handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    return true;
}

// One NVMe command through StorageDeviceProtocolSpecificProperty.
bool nvmeCommand(int diskNumber, DWORD dataType, DWORD requestValue, DWORD subValue,
                 DWORD dataLen, std::vector<unsigned char>& payload,
                 std::wstring& errorMessage)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD err = 0;
    if (!openDisk(diskNumber, h, L"r+w", err)) {
        errorMessage = wformat(L"Не удалось открыть \\\\.\\PhysicalDrive%d (код %u)",
                               diskNumber, static_cast<unsigned>(err));
        return false;
    }

    const size_t paramOffset = offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters);
    const size_t headerSize = paramOffset + sizeof(AppStorageProtocolSpecificDataExt);
    std::vector<unsigned char> buffer(headerSize + dataLen, 0);

    auto* query = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buffer.data());
    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;

    auto* proto = reinterpret_cast<AppStorageProtocolSpecificDataExt*>(buffer.data() + paramOffset);
    proto->ProtocolType = appProtocolNvme;
    proto->DataType = dataType;
    proto->ProtocolDataRequestValue = requestValue;
    proto->ProtocolDataRequestSubValue = subValue;
    proto->ProtocolDataOffset = static_cast<DWORD>(sizeof(AppStorageProtocolSpecificDataExt));
    proto->ProtocolDataLength = dataLen;

    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                                    buffer.data(), static_cast<DWORD>(buffer.size()),
                                    buffer.data(), static_cast<DWORD>(buffer.size()),
                                    &returned, nullptr);
    CloseHandle(h);

    if (!ok) {
        errorMessage = wformat(L"NVMe ioctl не удался (код %u)",
                               static_cast<unsigned>(GetLastError()));
        return false;
    }

    payload.assign(dataLen, 0);
    if (dataLen != 0)
        std::memcpy(payload.data(), buffer.data() + headerSize, dataLen);
    return true;
}

bool identifyController(int diskNumber, std::vector<unsigned char>& data,
                        std::wstring& errorMessage)
{
    std::vector<unsigned char> d(kIdentifySize, 0);
    if (!nvmeCommand(diskNumber, appNvmeDataTypeIdentify, 1 /* CNS=Controller */,
                     0 /* NSID */, kIdentifySize, d, errorMessage))
        return false;
    data.swap(d);
    return true;
}

bool identifyNamespace(int diskNumber, DWORD nsid, std::vector<unsigned char>& data,
                       std::wstring& errorMessage)
{
    std::vector<unsigned char> d(kIdentifySize, 0);
    if (!nvmeCommand(diskNumber, appNvmeDataTypeIdentify, 0 /* CNS=Namespace */,
                     nsid, kIdentifySize, d, errorMessage))
        return false;
    data.swap(d);
    return true;
}

bool smartHealth(int diskNumber, std::vector<unsigned char>& data,
                 std::wstring& errorMessage)
{
    std::vector<unsigned char> d(kSmartSize, 0);
    if (!nvmeCommand(diskNumber, appNvmeDataTypeLogPage, 2 /* Log 02h SMART */,
                     0 /* NSID */, kSmartSize, d, errorMessage))
        return false;
    data.swap(d);
    return true;
}

// Identify Namespace capacity: NSZE(low64) * 2^lbads.
bool namespaceCapacity(const std::vector<unsigned char>& ns, unsigned long long& bytes)
{
    if (ns.size() < 512)
        return false;
    const DWORD flbas = ns[26] & 0x0F;
    const DWORD lbafOff = 128 + flbas * 4;
    if (static_cast<size_t>(lbafOff) + 4 > ns.size())
        return false;
    const DWORD lbaf = le32(ns.data() + lbafOff);
    const DWORD lbads = lbaf & 0x3F; // log2 bytes per sector
    const unsigned long long nsze = le64(ns.data()); // low 64 bits of 128-bit NSZE
    bytes = nsze << lbads;
    return nsze > 0;
}

} // namespace

bool NvmeRegisters::populate(int diskNumber, StorageDevice& device, std::wstring& errorMessage)
{
    std::vector<unsigned char> ctrl;
    if (!identifyController(diskNumber, ctrl, errorMessage))
        return false;

    // Identity strings (ASCII, space padded).
    device.model = widen(ascii(ctrl.data() + 4, 40));
    device.serialNumber = widen(ascii(ctrl.data() + 68, 20));
    device.firmwareVersion = widen(ascii(ctrl.data() + 92, 8));
    if (device.friendlyName.empty())
        device.friendlyName = device.model;
    device.isNvme = true;
    device.ataIdentifyOk = false;
    device.ataDirectRegister = false;

    // Namespace 1 capacity.
    std::vector<unsigned char> ns;
    if (identifyNamespace(diskNumber, 1, ns, errorMessage)) {
        unsigned long long capacity = 0;
        if (namespaceCapacity(ns, capacity) && device.sizeBytes == 0)
            device.sizeBytes = capacity;
    }

    // SMART / Health Information log.
    std::vector<unsigned char> sm;
    if (smartHealth(diskNumber, sm, errorMessage) && sm.size() >= 512) {
        const int kelvin = static_cast<int>(le32(sm.data()) & 0xFFFF);
        if (kelvin > 0) {
            device.hasTemperature = true;
            device.temperatureC = kelvin - 273;
        }
        device.wearPercent = sm[4]; // Percentage Used
        device.powerOnHours = le64(sm.data() + 128);
        device.startStopCycleCount = static_cast<unsigned int>(le64(sm.data() + 112));
        device.readErrorsUncorrected = le64(sm.data() + 152); // Media & Data Integrity Errors
        device.hasReliability = (device.wearPercent >= 0 || device.powerOnHours > 0);
    }

    device.healthOk = true;
    return true;
}