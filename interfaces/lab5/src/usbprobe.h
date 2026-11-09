#pragma once
// Слой работы с libusb: инициализация контекста, получение списка устройств,
// разбор дескрипторов, подсчёт endpoint'ов, получение строковых дескрипторов.
// Все функции возвращают bool и пишут ошибки в stderr (требование: обработка ошибок).

#include <libusb-1.0/libusb.h>

#include "devtable.h"

namespace usbprobe {

// Обёртка над libusb_context: инициализирует libusb в конструкторе.
class Prober {
public:
    Prober();
    ~Prober();

    Prober(const Prober&) = delete;
    Prober& operator=(const Prober&) = delete;

    // Жив ли libusb (успешная инициализация)?
    bool ok() const { return ctx_ != nullptr; }
    const char* errorText() const { return error_; }

    // Полный снапшот видимой шины (соберёт информацию по каждому устройству).
    // devices — заполняется; при ошибках пишет в stderr, обходит сбойные узлы.
    bool snapshot(devtable::Snapshot& out);

    // Отладочный контекст (для SDK_INIT logs при необходимости).
    libusb_context* rawContext() const { return ctx_; }

private:
    // Заполнить DevInfo для конкретного устройства.
    void fillDevice(libusb_device* dev, devtable::DevInfo& info);

    libusb_context* ctx_ = nullptr;
    const char* error_ = "libusb not initialized";
};

}  // namespace usbprobe