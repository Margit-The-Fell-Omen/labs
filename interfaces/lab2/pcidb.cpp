#include "pcidb.h"

namespace {

struct VendorEntry {
    quint16 id;
    const char *name;
};

const VendorEntry kVendors[] = {
    {0x0001, "Innovative Semiconductor"},
    {0x0E11, "Compaq"},
    {0x1000, "Broadcom / LSI"},
    {0x1002, "AMD (ATI)"},
    {0x1013, "Cirrus Logic"},
    {0x1022, "AMD"},
    {0x1039, "Silicon Integrated Systems (SiS)"},
    {0x104C, "Texas Instruments"},
    {0x105A, "Promise Technology"},
    {0x1077, "QLogic"},
    {0x10B9, "ALi / ULi"},
    {0x10DE, "NVIDIA"},
    {0x10EC, "Realtek"},
    {0x1106, "VIA"},
    {0x111D, "Integrated Device Technology (IDT)"},
    {0x11AB, "Marvell"},
    {0x126F, "Silicon Motion"},
    {0x1344, "Micron"},
    {0x13C1, "Areca"},
    {0x144D, "Samsung"},
    {0x14E4, "Broadcom"},
    {0x15AD, "VMware"},
    {0x15B3, "Mellanox"},
    {0x15B7, "SanDisk / WD"},
    {0x168C, "Qualcomm Atheros"},
    {0x17CB, "Qualcomm"},
    {0x1912, "Renesas"},
    {0x1969, "Qualcomm Atheros (Killer)"},
    {0x1987, "Phison"},
    {0x1A03, "ASPEED"},
    {0x1AF4, "Red Hat / VirtIO"},
    {0x1B21, "ASMedia"},
    {0x1B36, "Red Hat (QXL)"},
    {0x1C5C, "SK hynix"},
    {0x1D6A, "Aquantia"},
    {0x1ED2, "Fungible"},
    {0x2646, "Kingston"},
    {0x8086, "Intel"},
    {0x80EE, "Oracle (VirtualBox)"},
    {0x1234, "QEMU / Bochs"},
};

} // namespace

QString vendorName(quint16 vendorId)
{
    for (const auto &v : kVendors) {
        if (v.id == vendorId)
            return QString::fromUtf8(v.name);
    }
    return QStringLiteral("Vendor %1h").arg(vendorId, 4, 16, QLatin1Char('0'));
}

QString className(quint8 baseClass, quint8 subClass, quint8 progIf)
{
    Q_UNUSED(progIf);
    switch (baseClass) {
    case 0x00: return QStringLiteral("Unclassified device");
    case 0x01: return QStringLiteral("Mass storage controller");
    case 0x02: return QStringLiteral("Network controller");
    case 0x03: return QStringLiteral("Display controller");
    case 0x04: return QStringLiteral("Multimedia controller");
    case 0x05: return QStringLiteral("Memory controller");
    case 0x06:
        switch (subClass) {
        case 0x00: return QStringLiteral("Host bridge");
        case 0x01: return QStringLiteral("ISA bridge");
        case 0x02: return QStringLiteral("EISA bridge");
        case 0x03: return QStringLiteral("MCA bridge");
        case 0x04: return QStringLiteral("PCI-to-PCI bridge");
        case 0x06: return QStringLiteral("CardBus bridge");
        case 0x07: return QStringLiteral("PCIe-to-PCI bridge");
        case 0x08: return QStringLiteral("Miscellaneous bridge");
        default:   return QStringLiteral("Bridge");
        }
    case 0x07: return QStringLiteral("Communication controller");
    case 0x08: return QStringLiteral("Base system peripheral");
    case 0x09: return QStringLiteral("Input device controller");
    case 0x0A: return QStringLiteral("Docking station");
    case 0x0B: return QStringLiteral("Processor");
    case 0x0C: return QStringLiteral("Serial bus controller");
    case 0x0D: return QStringLiteral("Wireless controller");
    case 0x0E: return QStringLiteral("Intelligent controller");
    case 0x0F: return QStringLiteral("Satellite communications controller");
    case 0x10: return QStringLiteral("Encryption/decryption controller");
    case 0x11: return QStringLiteral("Signal processing controller");
    case 0x12: return QStringLiteral("Processing accelerators");
    case 0x13: return QStringLiteral("Non-essential instrumentation");
    case 0x40: return QStringLiteral("Co-processor");
    case 0xFF: return QStringLiteral("Unassigned class");
    default:   return QStringLiteral("Device");
    }
}