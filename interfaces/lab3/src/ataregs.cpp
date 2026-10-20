#include "ataregs.h"

#include <ntddscsi.h>

#include <cstring>

#include "strutil.h"

namespace {

// ATA task-file register order (matches PreviousTaskFile[]/CurrentTaskFile[]).
enum AtaReg {
    regData = 0,
    regFeature = 1,      // Features on write, Error on read
    regSectorCount = 2,
    regSectorNumber = 3, // LBA low
    regCylLow = 4,       // LBA mid
    regCylHigh = 5,      // LBA high
    regDriveHead = 6,    // Device/Head
    regCommand = 7,      // Command on write, Status on read
};

bool passThroughOnce(int diskNumber,
                     BYTE command, BYTE features, BYTE sectorCount,
                     BYTE lbaLow, BYTE lbaMid, BYTE lbaHigh, BYTE deviceHead,
                     bool dataIn, std::vector<unsigned char>& dataBuffer,
                     unsigned char& statusRegister, unsigned char& errorRegister,
                     std::wstring& errorMessage)
{
    const ULONG dataBytes = static_cast<ULONG>(sectorCount) * 512UL;
    if (dataIn && dataBuffer.size() < dataBytes)
        dataBuffer.resize(dataBytes);

    const std::wstring path = wformat(L"\\\\.\\PhysicalDrive%d", diskNumber);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        errorMessage = wformat(L"Не удалось открыть \\\\.\\PhysicalDrive%d (ошибка %u)",
                               diskNumber, static_cast<unsigned>(GetLastError()));
        return false;
    }

    ATA_PASS_THROUGH_EX apt = {};
    apt.Length = sizeof(ATA_PASS_THROUGH_EX);
    apt.AtaFlags = dataIn ? ATA_FLAGS_DATA_IN : ATA_FLAGS_DRDY_REQUIRED;
    apt.DataTransferLength = dataIn ? dataBytes : 0;
    apt.TimeOutValue = 5;
    apt.DataBufferOffset = sizeof(ATA_PASS_THROUGH_EX);
    apt.PathId = 0;
    apt.TargetId = 0;
    apt.Lun = 0;

    // Write the task-file registers (input goes into PreviousTaskFile[]).
    apt.PreviousTaskFile[regFeature] = features;
    apt.PreviousTaskFile[regSectorCount] = sectorCount;
    apt.PreviousTaskFile[regSectorNumber] = lbaLow;
    apt.PreviousTaskFile[regCylLow] = lbaMid;
    apt.PreviousTaskFile[regCylHigh] = lbaHigh;
    apt.PreviousTaskFile[regDriveHead] = deviceHead;
    apt.PreviousTaskFile[regCommand] = command;

    std::vector<unsigned char> buffer(sizeof(ATA_PASS_THROUGH_EX) + (dataIn ? dataBytes : 0), 0);
    if (dataIn)
        std::memcpy(buffer.data() + sizeof(ATA_PASS_THROUGH_EX), dataBuffer.data(), dataBytes);

    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, IOCTL_ATA_PASS_THROUGH,
                                    &apt, sizeof(apt),
                                    buffer.data(), static_cast<DWORD>(buffer.size()),
                                    &returned, nullptr);
    if (!ok) {
        errorMessage = wformat(L"IOCTL_ATA_PASS_THROUGH cmd=0x%02X не удался (ошибка %u)",
                               static_cast<unsigned>(command),
                               static_cast<unsigned>(GetLastError()));
        CloseHandle(h);
        return false;
    }

    // Read back the resulting task-file registers (Command slot == Status,
    // Feature slot == Error).
    const ATA_PASS_THROUGH_EX* result =
        reinterpret_cast<const ATA_PASS_THROUGH_EX*>(buffer.data());
    statusRegister = result->CurrentTaskFile[regCommand];
    errorRegister = result->CurrentTaskFile[regFeature];

    if (dataIn)
        std::memcpy(dataBuffer.data(), buffer.data() + sizeof(ATA_PASS_THROUGH_EX), dataBytes);

    CloseHandle(h);
    return true;
}

} // namespace

bool AtaRegisters::taskFileIo(int diskNumber,
                              BYTE command, BYTE features, BYTE sectorCount,
                              BYTE lbaLow, BYTE lbaMid, BYTE lbaHigh, BYTE deviceHead,
                              bool dataIn, std::vector<unsigned char>& dataBuffer,
                              unsigned char& statusRegister, unsigned char& errorRegister,
                              std::wstring& errorMessage)
{
    return passThroughOnce(diskNumber, command, features, sectorCount,
                           lbaLow, lbaMid, lbaHigh, deviceHead, dataIn, dataBuffer,
                           statusRegister, errorRegister, errorMessage);
}

bool AtaRegisters::identifyDevice(int diskNumber, std::vector<unsigned char>& identifyData,
                                  unsigned char& statusRegister, unsigned char& errorRegister,
                                  std::wstring& errorMessage)
{
    identifyData.assign(512, 0);

    // IDENTIFY DEVICE (0xEC), 1 sector of data, LBA fields and Features = 0.
    bool ok = taskFileIo(diskNumber, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x00, 0xA0,
                         true, identifyData, statusRegister, errorRegister, errorMessage);
    // Retry device/head = 0xB0 (device 1) if the first attempt failed or the
    // Error bit in Status is set.
    if (!ok || (statusRegister & 0x01)) {
        ok = taskFileIo(diskNumber, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x00, 0xB0,
                        true, identifyData, statusRegister, errorRegister, errorMessage);
    }
    return ok;
}

bool AtaRegisters::decodeRotationRate(const std::vector<unsigned char>& identifyData,
                                      unsigned short& word217, bool& rotational)
{
    if (identifyData.size() < 512)
        return false;

    const unsigned char* base = identifyData.data() + 217 * 2;
    word217 = static_cast<unsigned short>(base[0] | (static_cast<unsigned short>(base[1]) << 8));

    if (word217 == 0x0000)
        return false;            // rate not reported
    if (word217 == 0x0001) {
        rotational = false;      // non-rotating media (SSD)
        return true;
    }
    rotational = (word217 >= 0x0401 && word217 != 0xFFFF);
    return true;
}

bool AtaRegisters::readSmartData(int diskNumber, std::vector<unsigned char>& smart512,
                                 unsigned char& statusRegister, unsigned char& errorRegister,
                                 std::wstring& errorMessage)
{
    // SMART READ DATA: Command 0xB0, Feature 0xD0, SectorCount 1,
    // LBA signature {0x4F, 0x43, 0x42}, Device/Head 0xA0.
    smart512.assign(512, 0);
    bool ok = taskFileIo(diskNumber, 0xB0, 0xD0, 0x01, 0x4F, 0x43, 0x42, 0xA0,
                         true, smart512, statusRegister, errorRegister, errorMessage);
    if (!ok || (statusRegister & 0x01))
        ok = taskFileIo(diskNumber, 0xB0, 0xD0, 0x01, 0x4F, 0x43, 0x42, 0xB0,
                        true, smart512, statusRegister, errorRegister, errorMessage);
    return ok;
}

bool AtaRegisters::parseSmartData(const std::vector<unsigned char>& smart512, AtaSmartInfo& out)
{
    if (smart512.size() < 2 + 30 * 12)
        return false;

    const unsigned char* attr = smart512.data() + 2; // 30 x 12-byte attributes
    for (int i = 0; i < 30; ++i, attr += 12) {
        const unsigned char id = attr[0];
        const unsigned char* raw = attr + 5; // 6-byte raw value (little-endian)
        auto rawValue = [raw]() -> unsigned long long {
            unsigned long long v = 0;
            for (int b = 0; b < 6; ++b)
                v |= static_cast<unsigned long long>(raw[b]) << (8 * b);
            return v;
        };

        switch (id) {
        case 9:      // Power-On Hours
            out.powerOnHours = rawValue();
            break;
        case 194:    // Current drive temperature
            out.temperatureC = static_cast<int>(raw[0]);
            break;
        case 202:    // 0xCA Percentage Used Endurance Indicator (SSD)
            if (rawValue() != 0xFFFFFFFFFFFFULL)
                out.wearPercent = static_cast<int>(rawValue());
            break;
        case 5:      // Reallocated Sectors Count
            out.reallocatedSectors = rawValue();
            break;
        case 197:    // Current Pending Sector
        case 198:    // Uncorrectable Sectors
            out.uncorrectableErrors = rawValue();
            break;
        case 4:      // Start/Stop Count
            out.startStopCount = static_cast<unsigned int>(rawValue() & 0xFFFFFFFFULL);
            break;
        default:
            break;
        }
    }

    out.haveData = true;
    return true;
}