#include "sysfs.h"

#include <dirent.h>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace sysfs {

namespace {
// Скобки-путь к каталогу /sys/bus/usb/devices.
const char* kSysUsbDir = "/sys/bus/usb/devices";
}  // namespace

std::string readAttribute(const std::string& devPath, const std::string& attr) {
    const std::string path = devPath + "/" + attr;
    std::ifstream f(path.c_str(), std::ios::in);
    if (!f)
        return std::string();
    std::string line;
    std::getline(f, line);
    // Удаляем завершающий перевод строки, если есть.
    if (!line.empty() && line.back() == '\n')
        line.pop_back();
    return line;
}

std::string findDevicePath(uint8_t bus, uint8_t addr) {
    DIR* dir = opendir(kSysUsbDir);
    if (!dir) {
        std::perror("sysfs: opendir /sys/bus/usb/devices");
        return std::string();
    }

    std::string result;
    struct dirent* de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        // Пропускаем "." и "..". Вложенные каталоги — устройства;
        // root-узлы вида "usb1" тоже учитываем (это корневые хабы).
        if (de->d_name[0] == '.')
            continue;

        // Каждый вложенный каталог — устройство.
        const std::string devPath = std::string(kSysUsbDir) + "/" + de->d_name;
        // Читаем busnum и devnum.
        const std::string busS = readAttribute(devPath, "busnum");
        const std::string addrS = readAttribute(devPath, "devnum");
        if (busS.empty() || addrS.empty())
            continue;

        // Сравниваем числа: busnum и devnum — это и есть (bus, addr) из libusb.
        if (static_cast<uint8_t>(std::stoi(busS)) == bus &&
            static_cast<uint8_t>(std::stoi(addrS)) == addr) {
            result = devPath;
            break;
        }
    }
    closedir(dir);
    return result;
}

void fillStrings(const std::string& devPath, devtable::DevInfo& info) {
    // sysfs: manufacturer -> производитель, product -> продукт, serial -> серийный.
    info.manufacturer = readAttribute(devPath, "manufacturer");
    info.product = readAttribute(devPath, "product");
    info.serial = readAttribute(devPath, "serial");
}

}  // namespace sysfs