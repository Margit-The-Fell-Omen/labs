#pragma once

#include "bmpwrite.h"

#include <string>

enum class SnapFormat
{
    Bmp,
    Jpeg,
};

bool saveSnapshot(const std::wstring& path, const RawFrame& frame, SnapFormat format);