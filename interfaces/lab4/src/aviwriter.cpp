#include "aviwriter.h"

#include <cstring>

namespace {

constexpr uint32_t kFccMjpg = 0x47504A4D;

} // namespace

AviWriter::~AviWriter()
{
    if (isOpen())
        finish();
}

void AviWriter::writeBytes(const void* data, std::size_t size)
{
    DWORD written = 0;
    WriteFile(m_file, data, static_cast<DWORD>(size), &written, nullptr);
}

void AviWriter::writeU32(uint32_t value)
{
    writeBytes(&value, 4);
}

void AviWriter::writeStr(const char fourcc[4])
{
    writeBytes(fourcc, 4);
}

void AviWriter::writeU16(std::uint16_t value)
{
    writeBytes(&value, 2);
}

void AviWriter::padEven()
{
    if (SetFilePointer(m_file, 0, nullptr, FILE_CURRENT) & 1) {
        const unsigned char zero = 0;
        writeBytes(&zero, 1);
    }
}

bool AviWriter::open(const std::wstring& path, int width, int height, double fps)
{
    if (isOpen())
        return false;

    m_file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_file == INVALID_HANDLE_VALUE)
        return false;

    if (fps <= 0.0)
        fps = 25.0;
    if (fps > 500.0)
        fps = 500.0;

    const uint32_t rate = static_cast<uint32_t>(fps + 0.5);
    const uint32_t microSec = static_cast<uint32_t>(1000000.0 / fps + 0.5);
    const uint32_t frameBytes = static_cast<uint32_t>((((width * 3 + 3) & ~3)) * height);
    const uint32_t aviBuffer = 1u << 20;
    const uint32_t bytePerSec =
        static_cast<uint32_t>(static_cast<uint64_t>(width) * height * 3 * rate);

    m_width = width;
    m_height = height;

    // RIFF AVI
    writeStr("RIFF");
    writeU32(0);
    writeStr("AVI ");

    // LIST hdrl
    writeStr("LIST");
    m_hdrlSizePos = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
    writeU32(0);
    writeStr("hdrl");

    // avih
    writeStr("avih");
    writeU32(56);
    {
        const uint32_t avihData = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
        m_patchAvihFrames = avihData + 16;
    }
    writeU32(microSec);
    writeU32(bytePerSec);
    writeU32(0);
    writeU32(0x930); // AVIF_HASINDEX|AVIF_MUSTUSEINDEX|AVIF_ISINTERLEAVED|AVIF_TRUSTCKTYPE
    writeU32(0);    // total frames, patched later
    writeU32(0);
    writeU32(1); // streams
    writeU32(aviBuffer);
    writeU32(static_cast<uint32_t>(width));
    writeU32(static_cast<uint32_t>(height));
    for (int i = 0; i < 4; ++i)
        writeU32(0);

    // LIST strl
    writeStr("LIST");
    m_strlSizePos = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
    writeU32(0);
    writeStr("strl");

    // strh
    writeStr("strh");
    writeU32(56);
    {
        const uint32_t strhData = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
        m_patchStrhLength = strhData + 32;
    }
    writeStr("vids");
    writeU32(kFccMjpg);
    writeU32(0);
    writeU32(0); // priority+language
    writeU32(0); // initial frames
    writeU32(1); // scale
    writeU32(rate);
    writeU32(0); // start
    writeU32(0); // length, patched later
    writeU32(frameBytes);
    writeU32(0xFFFFFFFFU); // quality
    writeU32(0);           // sample size
    writeU16(0);           // rcFrame.left
    writeU16(0);           // rcFrame.top
    writeU16(static_cast<std::uint16_t>(width));
    writeU16(static_cast<std::uint16_t>(height));

    // strf (BJPG BITMAPINFOHEADER)
    writeStr("strf");
    writeU32(40);
    writeU32(40);
    writeU32(static_cast<uint32_t>(width));
    writeU32(static_cast<uint32_t>(height));
    writeU16(1);  // planes
    writeU16(24); // bit count
    writeU32(kFccMjpg);
    writeU32(frameBytes);
    writeU32(0);
    writeU32(0);
    writeU32(0);
    writeU32(0);

    // LIST movi
    m_moviListPos = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
    writeStr("LIST");
    m_moviSizePos = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
    writeU32(0);
    writeStr("movi");
    m_moviDataPos = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);

    m_offsets.clear();
    m_sizes.clear();
    m_frameCount = 0;
    return true;
}

void AviWriter::addFrame(const void* data, std::size_t size)
{
    if (!isOpen() || !data || size == 0 || size > 0xFFFFFFFFU)
        return;

    const uint32_t dataPos = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);
    // Offsets are relative to the start of the 'movi' chunk's data section
    // as produced by mainstream muxers (first frame data => 4).
    const uint32_t dataOffset = (dataPos - m_moviDataPos) + 4;

    writeStr("00dc");
    writeU32(static_cast<uint32_t>(size));
    writeBytes(data, size);
    padEven();

    m_offsets.push_back(dataOffset);
    m_sizes.push_back(static_cast<uint32_t>(size));
    ++m_frameCount;
}

bool AviWriter::finish()
{
    if (!isOpen())
        return false;

    const uint32_t frameCount = static_cast<uint32_t>(m_frameCount);
    const uint32_t moviEnd = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);

    // Patch movi list size.
    const uint32_t moviSize = (moviEnd - m_moviDataPos) + 4;
    SetFilePointer(m_file, m_moviSizePos, nullptr, FILE_BEGIN);
    writeU32(moviSize);

    // Patch avih total frames and strh stream length.
    SetFilePointer(m_file, m_patchAvihFrames, nullptr, FILE_BEGIN);
    writeU32(frameCount);
    SetFilePointer(m_file, m_patchStrhLength, nullptr, FILE_BEGIN);
    writeU32(frameCount);

    // idx1
    SetFilePointer(m_file, moviEnd, nullptr, FILE_BEGIN);
    writeStr("idx1");
    writeU32(static_cast<uint32_t>(m_frameCount * 16));
    for (std::size_t i = 0; i < m_frameCount; ++i) {
        writeStr("00dc");
        writeU32(0x10); // AVIIF_KEYFRAME
        writeU32(m_offsets[i]);
        writeU32(m_sizes[i]);
    }

    const uint32_t fileEnd = SetFilePointer(m_file, 0, nullptr, FILE_CURRENT);

    // Patch hdrl / strl / riff sizes.
    const uint32_t strlSize = (m_moviListPos - m_strlSizePos) - 4;
    const uint32_t hdrlSize = (m_moviListPos - m_hdrlSizePos) - 4;
    const uint32_t riffSize = fileEnd - 8;

    SetFilePointer(m_file, m_strlSizePos, nullptr, FILE_BEGIN);
    writeU32(strlSize);
    SetFilePointer(m_file, m_hdrlSizePos, nullptr, FILE_BEGIN);
    writeU32(hdrlSize);
    SetFilePointer(m_file, 4, nullptr, FILE_BEGIN);
    writeU32(riffSize);

    CloseHandle(m_file);
    m_file = INVALID_HANDLE_VALUE;
    return true;
}