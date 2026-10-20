#pragma once

#include "storage.h"

#include <string>
#include <vector>

namespace AtaDirect {

// True when this build is 32-bit i386 (the I/O port instructions are only
// available there).
bool supported();

// Reads IDENTIFY DEVICE by writing and reading the legacy ATA task-file
// registers on the given port base (0x1F0 = primary channel, 0x3F0 =
// secondary). On success `identifyData` holds 512 bytes and the Status/Error
// registers are returned for display.
bool identifyViaRegisterPorts(unsigned short basePort,
                              std::vector<unsigned char>& identifyData,
                              unsigned char& statusRegister, unsigned char& errorRegister,
                              std::wstring& errorMessage);

// Reads the 512-byte S.M.A.R.T. attribute table (READ DATA, command 0xB0)
// directly through the same task-file registers.
bool readSmartViaRegisterPorts(unsigned short basePort,
                               std::vector<unsigned char>& smartData,
                               unsigned char& statusRegister, unsigned char& errorRegister,
                               std::wstring& errorMessage);
}
