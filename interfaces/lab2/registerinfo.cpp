#include "registerinfo.h"

namespace {

QVector<BitField> makeCommandBits()
{
    return {
        {0,  QStringLiteral("I/O Space Enable")},
        {1,  QStringLiteral("Memory Space Enable")},
        {2,  QStringLiteral("Bus Mastering")},
        {3,  QStringLiteral("Special Cycles")},
        {4,  QStringLiteral("Memory Write and Invalidate Enable")},
        {5,  QStringLiteral("VGA Palette Snoop")},
        {6,  QStringLiteral("Parity Error Response")},
        {7,  QStringLiteral("IDSEL Stepping")},
        {8,  QStringLiteral("SERR# Enable")},
        {9,  QStringLiteral("Fast Back-to-Back Enable")},
        {10, QStringLiteral("Interrupt Disable")},
        {11, QStringLiteral("Reserved")},
        {12, QStringLiteral("Reserved")},
        {13, QStringLiteral("Reserved")},
        {14, QStringLiteral("Reserved")},
        {15, QStringLiteral("Reserved")},
    };
}

QVector<BitField> makeStatusBits()
{
    return {
        {0,  QStringLiteral("Reserved")},
        {1,  QStringLiteral("Reserved")},
        {2,  QStringLiteral("Reserved")},
        {3,  QStringLiteral("Interrupt Status")},
        {4,  QStringLiteral("Capabilities List")},
        {5,  QStringLiteral("66 MHz Capable")},
        {6,  QStringLiteral("UDF Supported")},
        {7,  QStringLiteral("Fast Back-to-Back Capable")},
        {8,  QStringLiteral("Master Data Parity Error")},
        {9,  QStringLiteral("DEVSEL Timing (bit 0)")},
        {10, QStringLiteral("DEVSEL Timing (bit 1)")},
        {11, QStringLiteral("Signaled Target Abort")},
        {12, QStringLiteral("Received Target Abort")},
        {13, QStringLiteral("Received Master Abort")},
        {14, QStringLiteral("Signaled System Error")},
        {15, QStringLiteral("Detected Parity Error")},
    };
}

QVector<RegisterField> makeHeaderFields()
{
    return {
        {0x00, 2, QStringLiteral("Vendor ID")},
        {0x02, 2, QStringLiteral("Device ID")},
        {0x04, 2, QStringLiteral("Command")},
        {0x06, 2, QStringLiteral("Status")},
        {0x08, 1, QStringLiteral("Revision ID")},
        {0x09, 3, QStringLiteral("Class Code")},
        {0x0C, 1, QStringLiteral("Cache Line Size")},
        {0x0D, 1, QStringLiteral("Latency Timer")},
        {0x0E, 1, QStringLiteral("Header Type")},
        {0x0F, 1, QStringLiteral("BIST")},
        {0x10, 4, QStringLiteral("BAR0")},
        {0x14, 4, QStringLiteral("BAR1")},
        {0x18, 4, QStringLiteral("BAR2")},
        {0x1C, 4, QStringLiteral("BAR3")},
        {0x20, 4, QStringLiteral("BAR4")},
        {0x24, 4, QStringLiteral("BAR5")},
        {0x28, 4, QStringLiteral("CardBus CIS Pointer")},
        {0x2C, 4, QStringLiteral("Subsystem Vendor/Device ID")},
        {0x30, 4, QStringLiteral("Expansion ROM Base Address")},
        {0x34, 1, QStringLiteral("Capabilities Pointer")},
        {0x3C, 1, QStringLiteral("Interrupt Line")},
        {0x3D, 1, QStringLiteral("Interrupt PIN")},
        {0x3E, 1, QStringLiteral("Min Grant")},
        {0x3F, 1, QStringLiteral("Max Latency")},
    };
}

} // namespace

const QVector<BitField> &commandBits()
{
    static const QVector<BitField> bits = makeCommandBits();
    return bits;
}

const QVector<BitField> &statusBits()
{
    static const QVector<BitField> bits = makeStatusBits();
    return bits;
}

const QVector<RegisterField> &headerFields()
{
    static const QVector<RegisterField> fields = makeHeaderFields();
    return fields;
}