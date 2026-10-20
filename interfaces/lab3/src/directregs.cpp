#include "directregs.h"

#include "strutil.h"

#include <windows.h>

#include <cstdint>

// Legacy ATA task-file register offsets from the channel base port
// (0x1F0 = primary channel, 0x3F0 = alternate).
enum LegacyPortOffset {
    offData = 0,        // Data register (read IDENTIFY words here)
    offFeatures = 1,    // Features on write, Error on read
    offSectorCount = 2,
    offSectorNumber = 3, // LBA low
    offCylLow = 4,       // LBA mid
    offCylHigh = 5,      // LBA high
    offDriveHead = 6,    // Device/Head
    offStatusCmd = 7,    // Command on write, Status on read
};

// Status register bits.
enum StatusBits {
    statusErr = 0x01,
    statusDrq = 0x08, // DRQ: data transfer request
    statusDrdy = 0x40, // DRDY
    statusBsy = 0x80,  // BSY
};

#if defined(__i386__) && defined(_WIN32)
#define ATA_I386 1
#else
#define ATA_I386 0
#endif

#if ATA_I386

static volatile LONG g_ioActive = 0;
static volatile LONG g_ioFaulted = 0;
static volatile LONG g_ioKnownUnavailable = 0;

// Catches the #GP/access violation raised by a ring-3 in/out instruction when
// Windows denies I/O-port access, skips the (single) faulting instruction and
// flags "direct I/O unavailable" so the app silently falls back to the OS
// register interface instead of crashing.
static LONG CALLBACK PortFaultHandler(PEXCEPTION_POINTERS ep)
{
    if (!g_ioActive)
        return EXCEPTION_CONTINUE_SEARCH;

    const ULONG code = ep->ExceptionRecord->ExceptionCode;
    if (code != STATUS_ACCESS_VIOLATION && code != STATUS_PRIVILEGED_INSTRUCTION
        && code != STATUS_ILLEGAL_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;

    // All our ports are >= 0x100, so GCC always emits the %dx form:
    //   0xEC  in  (%dx),%al    0xEE  out %al,(%dx)
    //   66 EC  inw (%dx),%ax   66 EE out %ax,(%dx)
    // Advance EIP past that single instruction.
    BYTE* ip = reinterpret_cast<BYTE*>(ep->ContextRecord->Eip);
    const unsigned len = (*ip == 0x66 || *ip == 0xEC || *ip == 0xEE) ? (*ip == 0x66 ? 2 : 1) : 0;
    if (len == 0)
        return EXCEPTION_CONTINUE_SEARCH;

    ep->ContextRecord->Eip += len;
    g_ioFaulted = 1;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void ensureFaultHandler()
{
    static bool registered = false;
    if (!registered) {
        if (AddVectoredExceptionHandler(1, PortFaultHandler))
            registered = true;
    }
}

static inline void rawOutb(unsigned short port, unsigned char value)
{
    __asm__ __volatile__("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

static inline unsigned char rawInb(unsigned short port)
{
    unsigned char v = 0;
    __asm__ __volatile__("inb %w1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline unsigned short rawInw(unsigned short port)
{
    unsigned short v = 0;
    __asm__ __volatile__("inw %w1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static bool safeOutb(unsigned short port, unsigned char value)
{
    if (g_ioKnownUnavailable)
        return false;
    ensureFaultHandler();
    g_ioFaulted = 0;
    g_ioActive = 1;
    rawOutb(port, value);
    g_ioActive = 0;
    if (g_ioFaulted) {
        g_ioKnownUnavailable = 1;
        return false;
    }
    return true;
}

static bool safeInb(unsigned short port, unsigned char& value)
{
    if (g_ioKnownUnavailable) {
        value = 0xFF;
        return false;
    }
    ensureFaultHandler();
    g_ioFaulted = 0;
    g_ioActive = 1;
    value = rawInb(port);
    g_ioActive = 0;
    if (g_ioFaulted) {
        g_ioKnownUnavailable = 1;
        value = 0xFF;
        return false;
    }
    return true;
}

static bool safeInw(unsigned short port, unsigned short& value)
{
    if (g_ioKnownUnavailable) {
        value = 0xFFFF;
        return false;
    }
    ensureFaultHandler();
    g_ioFaulted = 0;
    g_ioActive = 1;
    value = rawInw(port);
    g_ioActive = 0;
    if (g_ioFaulted) {
        g_ioKnownUnavailable = 1;
        value = 0xFFFF;
        return false;
    }
    return true;
}

static bool waitState(unsigned short base, unsigned char mask, bool set, unsigned int timeoutMs)
{
    const DWORD deadline = GetTickCount() + timeoutMs;
    do {
        unsigned char st = 0xFF;
        if (!safeInb(static_cast<unsigned short>(base + offStatusCmd), st))
            return false; // direct I/O denied -> abandon probe
        if (((st & mask) != 0) == set)
            return true;
        Sleep(5);
    } while (GetTickCount() < deadline);
    return false;
}

// Generic single-sector data-read over the task-file registers (used by both
// IDENTIFY DEVICE and SMART READ DATA).
static bool ataDataInPorts(unsigned short base,
                           unsigned char feature, unsigned char lbaLow, unsigned char lbaMid,
                           unsigned char lbaHigh, unsigned char command,
                           std::vector<unsigned char>& dataOut512,
                           unsigned char& statusRegister, unsigned char& errorRegister,
                           std::wstring& errorMessage)
{
    if (g_ioKnownUnavailable) {
        errorMessage = widen("Прямой доступ к I/O-портам запрещён Windows — запасной путь через IOCTL");
        return false;
    }

    // 1. Controller must be idle first (BSY clear). Short probe timeout.
    if (!waitState(base, statusBsy, false, 300)) {
        errorMessage = widen("Нет реакции на портах (таймаут BSY)");
        return false;
    }

    // 2. Write the task-file registers directly through the I/O ports.
    if (!safeOutb(static_cast<unsigned short>(base + offFeatures), feature)) goto denied;
    if (!safeOutb(static_cast<unsigned short>(base + offSectorCount), 0x01)) goto denied;
    if (!safeOutb(static_cast<unsigned short>(base + offSectorNumber), lbaLow)) goto denied;
    if (!safeOutb(static_cast<unsigned short>(base + offCylLow), lbaMid)) goto denied;
    if (!safeOutb(static_cast<unsigned short>(base + offCylHigh), lbaHigh)) goto denied;
    if (!safeOutb(static_cast<unsigned short>(base + offDriveHead), 0xA0)) goto denied;
    if (!safeOutb(static_cast<unsigned short>(base + offStatusCmd), command)) goto denied;

    // 3. Wait until the drive requests a data transfer (DRQ).
    if (!waitState(base, statusDrq, true, 700)) {
        safeInb(static_cast<unsigned short>(base + offFeatures), errorRegister);
        safeInb(static_cast<unsigned short>(base + offStatusCmd), statusRegister);
        errorMessage = widen("DRQ не установлен (устройство не отвечает на прямые порты)");
        return false;
    }

    if (!safeInb(static_cast<unsigned short>(base + offStatusCmd), statusRegister))
        goto denied;
    if (statusRegister & statusErr) {
        safeInb(static_cast<unsigned short>(base + offFeatures), errorRegister);
        errorMessage = widen("Ошибка накопителя при чтении через порты");
        return false;
    }

    // 4. Read the 256 words from the Data register.
    dataOut512.assign(512, 0);
    for (int i = 0; i < 256; ++i) {
        unsigned short w = 0;
        if (!safeInw(static_cast<unsigned short>(base + offData), w))
            goto denied;
        dataOut512[static_cast<size_t>(i) * 2] = static_cast<unsigned char>(w & 0xFF);
        dataOut512[static_cast<size_t>(i) * 2 + 1] = static_cast<unsigned char>(w >> 8);
    }

    // 5. Wait for command completion.
    if (waitState(base, statusBsy, false, 300)) {
        safeInb(static_cast<unsigned short>(base + offStatusCmd), statusRegister);
        safeInb(static_cast<unsigned short>(base + offFeatures), errorRegister);
        return true;
    }

    safeInb(static_cast<unsigned short>(base + offStatusCmd), statusRegister);
    safeInb(static_cast<unsigned short>(base + offFeatures), errorRegister);
    return true;

denied:
    errorMessage = widen("Прямой доступ к I/O-портам запрещён Windows — запасной путь через IOCTL");
    return false;
}

static bool identifyPorts(unsigned short base,
                          std::vector<unsigned char>& identifyData,
                          unsigned char& statusRegister, unsigned char& errorRegister,
                          std::wstring& errorMessage)
{
    return ataDataInPorts(base, 0x00, 0x00, 0x00, 0x00, 0xEC,
                          identifyData, statusRegister, errorRegister, errorMessage);
}

// SMART READ DATA (command 0xB0, feature 0xD0, LBA signature {0x4F,0x43,0x42}).
static bool smartPorts(unsigned short base,
                       std::vector<unsigned char>& smartData,
                       unsigned char& statusRegister, unsigned char& errorRegister,
                       std::wstring& errorMessage)
{
    return ataDataInPorts(base, 0xD0, 0x4F, 0x43, 0x42, 0xB0,
                          smartData, statusRegister, errorRegister, errorMessage);
}

#else

static bool identifyPorts(unsigned short, std::vector<unsigned char>&,
                          unsigned char&, unsigned char&, std::wstring& errorMessage)
{
    errorMessage = widen("Прямые порты доступны только в 32-битном режиме");
    return false;
}

static bool smartPorts(unsigned short, std::vector<unsigned char>&,
                       unsigned char&, unsigned char&, std::wstring& errorMessage)
{
    errorMessage = widen("Прямые порты доступны только в 32-битном режиме");
    return false;
}

#endif

bool AtaDirect::supported()
{
#if ATA_I386
    return true;
#else
    return false;
#endif
}

bool AtaDirect::identifyViaRegisterPorts(unsigned short basePort,
                                         std::vector<unsigned char>& identifyData,
                                         unsigned char& statusRegister,
                                         unsigned char& errorRegister,
                                         std::wstring& errorMessage)
{
#if ATA_I386
    return identifyPorts(basePort, identifyData, statusRegister, errorRegister, errorMessage);
#else
    errorMessage = widen("Прямые порты доступны только в 32-битном режиме");
    return false;
#endif
}

bool AtaDirect::readSmartViaRegisterPorts(unsigned short basePort,
                                          std::vector<unsigned char>& smartData,
                                          unsigned char& statusRegister,
                                          unsigned char& errorRegister,
                                          std::wstring& errorMessage)
{
#if ATA_I386
    return smartPorts(basePort, smartData, statusRegister, errorRegister, errorMessage);
#else
    errorMessage = widen("Прямые порты доступны только в 32-битном режиме");
    return false;
#endif
}