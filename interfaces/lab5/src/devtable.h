#pragma once
// Модель устройства: ключ (bus:addr) и интересующие нас поля.
// Используется и для разовых выводов (--once), и для сравнения снапшотов при
// отслеживании горячего подключения (devtable).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace devtable {

// Ключ, однозначно идентифицирующий USB-устройство на шине.
struct DevKey {
    uint8_t bus = 0;   // номер шины (libusb_get_bus_number)
    uint8_t addr = 0;  // адрес устройства (libusb_get_device_address)

    // Нужно std::map<DevKey,...> упорядочение.
    bool operator<(const DevKey& o) const {
        if (bus != o.bus) return bus < o.bus;
        return addr < o.addr;
    }
    bool operator==(const DevKey& o) const {
        return bus == o.bus && addr == o.addr;
    }
};

// Собранная о устройстве информация.
struct DevInfo {
    DevKey key;

    uint16_t vid = 0;        // idVendor
    uint16_t pid = 0;        // idProduct
    uint8_t devClass = 0;    // bDeviceClass (0x03 = HID)
    uint8_t devProtocol = 0; // bDeviceProtocol
    int endpointCount = 0;   // всего endpoint'ов во всех altsettings
    bool isHid = false;      // true, если само устройство или канал интерфейса class 0x03

    std::string manufacturer;
    std::string product;
    std::string serial;

    // Удобная строковая форма: "bus:addr vid:pid class protocol ep n isHid "
    std::string summaryLine() const;
};

// Один снапшот всей видимой шины: ключ -> инфо.
using Snapshot = std::map<DevKey, DevInfo>;

// Результат сравнения двух снапшотов: какие устройства добавились/исчезли.
struct Diff {
    std::vector<DevInfo> added;
    std::vector<DevInfo> removed;
    explicit operator bool() const { return !added.empty() || !removed.empty(); }
};

// Вычислить разницу двух снапшотов: "было" -> "стало".
Diff diff(const Snapshot& before, const Snapshot& after);

}  // namespace devtable