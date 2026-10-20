#pragma once

#include "storage.h"

#include <cstdint>
#include <string>
#include <vector>

// Low-level NVMe admin-command access through the OS storage protocol interface
// (IOCTL_STORAGE_QUERY_PROPERTY + StorageDeviceProtocolSpecificProperty).
// The controller's real registers (BAR0 MMIO, ASQ/ACQ, doorbells) are driven by
// the Windows NVMe stack; from user mode this is the lowest legal level.
//
// NVME Fixed-Protocol payloads (see NVMe spec 1.4):
//   Identify Controller  (CNS=1) -> model/serial/firmware, namespace count
//   Identify Namespace   (CNS=0) -> capacity (nsze * 2^lbads)
//   SMART/Health Log (LogPage 02h) -> temperature, % used, power-on hours, errors
namespace NvmeRegisters {

// ATA-only identifiers read from the NVMe SMART log / Identify pages.
struct IdentifyController;
struct IdentifyNamespace;
struct SmartHealthLog;

// Reads NVMe Identify Controller; fills `name/serial/firmware` on the device.
bool populate(int diskNumber, StorageDevice& device, std::wstring& errorMessage);

} // namespace NvmeRegisters