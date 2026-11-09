#pragma once
// Доступ к sysfs: поиск каталога /sys/bus/usb/devices/*, отвечающего устройству
// с заданными (bus, addr), и чтение строковых атрибутов (manufacturer, product,
// serial и т.п.). Используется как источник строк, если libusb-дескрипторы
// нечитаемы из-за прав usbfs.

#include <string>

#include "devtable.h"

namespace sysfs {

// Найти путь каталога в /sys/bus/usb/devices, чей devnum совпадает с (bus, addr).
// Возвращает путь без завершающего '/', или пустую строку.
std::string findDevicePath(uint8_t bus, uint8_t addr);

// Прочитать атрибут (файл) из каталога устройства. Возвращает строку
// без финального перевода строки. При ошибке — пустую строку.
std::string readAttribute(const std::string& devPath, const std::string& attr);

// Прочитать manufacturer/product/serial при возможности из sysfs.
void fillStrings(const std::string& devPath, devtable::DevInfo& info);

}  // namespace sysfs