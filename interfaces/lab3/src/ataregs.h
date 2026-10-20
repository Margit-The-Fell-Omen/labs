#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace AtaRegisters {

bool taskFileIo(int diskNumber,
                BYTE command, BYTE features, BYTE sectorCount,
                BYTE lbaLow, BYTE lbaMid, BYTE lbaHigh, BYTE deviceHead,
                bool dataIn, std::vector<unsigned char>& dataBuffer,
                unsigned char& statusRegister, unsigned char& errorRegister,
                std::wstring& errorMessage);

bool identifyDevice(int diskNumber, std::vector<unsigned char>& identifyData,
                    unsigned char& statusRegister, unsigned char& errorRegister,
                    std::wstring& errorMessage);

bool decodeRotationRate(const std::vector<unsigned char>& identifyData,
                        unsigned short& word217, bool& rotational);

// Decoded S.M.A.R.T. attribute values relevant to the comparison.
struct AtaSmartInfo {
    bool haveData = false;
    int temperatureC = -1;            // attribute 194
    unsigned long long powerOnHours = 0; // attribute 9
    int wearPercent = -1;             // attribute 202 (0xCA) - SSD endurance used
    unsigned long long reallocatedSectors = 0; // attribute 5
    unsigned long long uncorrectableErrors = 0; // attribute 197/198
    unsigned int startStopCount = 0;  // attribute 4
};

// S.M.A.R.T. READ DATA (command 0xB0, feature 0xD0) via IOCTL_ATA_PASS_THROUGH.
bool readSmartData(int diskNumber, std::vector<unsigned char>& smart512,
                   unsigned char& statusRegister, unsigned char& errorRegister,
                   std::wstring& errorMessage);

// Parses the 512-byte attribute table.
bool parseSmartData(const std::vector<unsigned char>& smart512, AtaSmartInfo& out);
}
