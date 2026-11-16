#pragma once
// Модель устройства и сравнение снимков шины.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace devtable {

struct DevKey {
    uint8_t bus = 0;
    uint8_t addr = 0;

    bool operator<(const DevKey& o) const {
        if (bus != o.bus) return bus < o.bus;
        return addr < o.addr;
    }
    bool operator==(const DevKey& o) const {
        return bus == o.bus && addr == o.addr;
    }
};

struct DevInfo {
    DevKey key;

    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t devClass = 0;
    uint8_t devProtocol = 0;
    int endpointCount = 0;
    bool isHid = false;

    std::string manufacturer;
    std::string product;
    std::string serial;

    std::string summaryLine() const;
};

using Snapshot = std::map<DevKey, DevInfo>;

struct Diff {
    std::vector<DevInfo> added;
    std::vector<DevInfo> removed;
    explicit operator bool() const { return !added.empty() || !removed.empty(); }
};

Diff diff(const Snapshot& before, const Snapshot& after);

}  // namespace devtable