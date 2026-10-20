#pragma once

#include <string>

enum class DriveType {
    Unknown,
    HDD,
    SSD,
};

// Aggregated view of a single physical disk, assembled entirely from low-level
// interfaces: DeviceIoControl storage descriptors, ATA task-file registers /
// SMART (direct ports + IOCTL_ATA_PASS_THROUGH) and NVMe admin commands
// (Identify + SMART log via the storage protocol interface).
struct StorageDevice {
    int diskNumber = -1; // corresponds to \\.\PhysicalDriveN

    std::wstring friendlyName;
    std::wstring model;
    std::wstring serialNumber;
    std::wstring firmwareVersion;

    DriveType type = DriveType::Unknown;
    std::wstring typeDetail; // human-readable basis for the type detection

    unsigned long long sizeBytes = 0;
    int busType = 0; // STORAGE_BUS_TYPE value (0x00..0x12)
    std::wstring busTypeName;

    // Type signals (StorageDeviceSeekPenaltyProperty / TrimProperty / RPM).
    bool seekPenaltyKnown = false;
    bool seekPenalty = false; // true -> rotational
    bool trimKnown = false;
    bool trimSupported = false;
    bool rpmKnown = false;
    unsigned int rpm = 0; // 0 => non-rotational or unknown
    bool isNvme = false;  // busType == NVMe

    // Reliability data, read at the lowest available level:
    //  - ATA  -> S.M.A.R.T. attribute table (temp=194, hours=9, wear=202, ...)
    //  - NVMe -> SMART/Health log page
    bool hasTemperature = false;
    int temperatureC = -1; // -1 => N/A

    bool hasReliability = false;
    unsigned long long powerOnHours = 0;
    int wearPercent = -1; // -1 => N/A; 0..100, 100 == wear limit reached
    unsigned long long readErrorsCorrected = 0;
    unsigned long long readErrorsUncorrected = 0;
    unsigned long long writeErrorsCorrected = 0;
    unsigned long long writeErrorsUncorrected = 0;
    unsigned int startStopCycleCount = 0;
    bool healthOk = false; // low-level health probe succeeded

    // Raw ATA register read-back from IDENTIFY DEVICE (see ataregs.h).
    bool ataIdentifyOk = false;
    unsigned char ataStatusRegister = 0;   // read from Command register slot
    unsigned char ataErrorRegister = 0;    // read from Features register slot
    unsigned short ataRotationRateWord = 0; // IDENTIFY word 217
    bool ataDirectRegister = false;        // true = read via outb/inb ports

    bool detectedByWinApi = false;

    std::wstring displayName() const;
};

std::wstring driveTypeToString(DriveType type);
std::wstring busTypeToString(int busType);
std::wstring formatBytes(unsigned long long bytes);
std::wstring formatHours(unsigned long long hours);
std::wstring formatRpm(unsigned int rpm, bool known);