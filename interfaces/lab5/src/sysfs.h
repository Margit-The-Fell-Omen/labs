#pragma once
// Доступ к sysfs: каталог устройства и строковые атрибуты.

#include <string>

#include "devtable.h"

namespace sysfs {

// Путь каталога устройства по (bus, addr) или пустая строка.
std::string findDevicePath(uint8_t bus, uint8_t addr);

// Значение атрибута без финального '\n' (пустая строка при ошибке).
std::string readAttribute(const std::string& devPath, const std::string& attr);

// manufacturer/product/serial из sysfs.
void fillStrings(const std::string& devPath, devtable::DevInfo& info);

}  // namespace sysfs