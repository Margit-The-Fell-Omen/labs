#pragma once

#include "storage.h"

#include <string>
#include <vector>

struct TypeSignals {
    bool seekPenaltyKnown = false;
    bool seekPenalty = false;
    bool trimKnown = false;
    bool trimSupported = false;
    bool rpmKnown = false;
    unsigned int rpm = 0;
    bool isNvme = false; // busType == NVMe
};

struct CompareRow {
    enum Flag { None = 0, LeftBetter = 1, RightBetter = 2 };
    std::wstring label;
    std::wstring left;
    std::wstring right;
    int flags = None;
};

struct Advice {
    enum Level { Info, Warning, Critical };
    Level level = Info;
    int diskNumber = -1;
    std::wstring text;
};

DriveType detectType(const TypeSignals& s);
std::wstring typeDetail(const TypeSignals& s);

// Builds the parameter-by-parameter comparison of two disks.
std::vector<CompareRow> compareDevices(const StorageDevice& left, const StorageDevice& right);

// Health-based recommendations for a single disk.
std::vector<Advice> recommendationsFor(const StorageDevice& device);

// Comparative advice (SSD vs HDD, wear difference, etc.).
std::vector<Advice> comparativeAdvice(const StorageDevice& left, const StorageDevice& right);