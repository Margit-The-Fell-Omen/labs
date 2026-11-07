#pragma once

#include <string>

struct RawFrame
{
    const void* data = nullptr;
    std::size_t size = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    int channels = 3;
    unsigned long fourcc = 0;
};

bool writeBmpFile(const std::wstring& path, const RawFrame& frame);