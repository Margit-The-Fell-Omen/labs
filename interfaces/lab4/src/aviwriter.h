#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

class AviWriter
{
public:
    AviWriter() = default;
    ~AviWriter();

    bool open(const std::wstring& path, int width, int height, double fps);
    void addFrame(const void* data, std::size_t size);
    bool finish();
    bool isOpen() const { return m_file != INVALID_HANDLE_VALUE; }

private:
    void writeBytes(const void* data, std::size_t size);
    void writeU32(std::uint32_t value);
    void writeStr(const char fourcc[4]);
    void writeU16(std::uint16_t value);
    void padEven();

HANDLE m_file = INVALID_HANDLE_VALUE;
    uint32_t m_moviDataPos = 0;
    uint32_t m_moviListPos = 0;
    uint32_t m_moviSizePos = 0;
    uint32_t m_hdrlSizePos = 0;
    uint32_t m_strlSizePos = 0;
    uint32_t m_patchAvihFrames = 0;
    uint32_t m_patchStrhLength = 0;
    std::size_t m_frameCount = 0;
    std::vector<uint32_t> m_offsets;
    std::vector<uint32_t> m_sizes;
    int m_width = 0;
    int m_height = 0;
};