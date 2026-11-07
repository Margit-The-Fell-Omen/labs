#include "bmpwrite.h"

#include <cstdint>
#include <vector>
#include <windows.h>

bool writeBmpFile(const std::wstring& path, const RawFrame& frame)
{
    if (!frame.data || frame.width <= 0 || frame.height <= 0)
        return false;
    if (frame.channels != 3 && frame.channels != 4)
        return false;

    const int bytesPerPixel = frame.channels;
    const int rowBytes = frame.width * bytesPerPixel;
    const int paddedRow = (rowBytes + 3) & ~3;
    const int stride = frame.stride > 0 ? frame.stride : rowBytes;

    std::vector<unsigned char> out;
    out.reserve(static_cast<std::size_t>(54 + paddedRow * frame.height));

    auto push8 = [&out](unsigned char v) { out.push_back(v); };
    auto push16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<unsigned char>(v & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    };
    auto push32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
    };

    // BITMAPFILEHEADER (14 bytes) + BITMAPINFOHEADER (40 bytes), packed.
    push16(0x4D42); // "BM"
    push32(static_cast<std::uint32_t>(54 + paddedRow * frame.height));
    push16(0);
    push16(0);
    push32(54); // pixel data offset

    push32(40); // biSize
    push32(static_cast<std::uint32_t>(frame.width));
    push32(static_cast<std::uint32_t>(frame.height)); // bottom-up
    push16(1);  // planes
    push16(static_cast<std::uint16_t>(bytesPerPixel * 8));
    push32(0); // BI_RGB
    push32(static_cast<std::uint32_t>(paddedRow * frame.height));
    push32(0);
    push32(0);
    push32(0);
    push32(0);

    const unsigned char* src = static_cast<const unsigned char*>(frame.data);
    for (int y = frame.height - 1; y >= 0; --y) {
        const unsigned char* row = src + static_cast<std::size_t>(y) * stride;
        out.insert(out.end(), row, row + rowBytes);
        static const unsigned char pad[4] = {0, 0, 0, 0};
        out.insert(out.end(), pad, pad + (paddedRow - rowBytes));
    }

    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == out.size();
}