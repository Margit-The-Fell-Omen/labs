#pragma once
// Работа с libusb: перечисление устройств, дескрипторы, endpoint'ы.

#include <libusb-1.0/libusb.h>

#include "devtable.h"

namespace usbprobe {

class Prober {
public:
    Prober();
    ~Prober();

    Prober(const Prober&) = delete;
    Prober& operator=(const Prober&) = delete;

    bool ok() const { return ctx_ != nullptr; }
    const char* errorText() const { return error_; }

    bool snapshot(devtable::Snapshot& out);

    libusb_context* rawContext() const { return ctx_; }

private:
    void fillDevice(libusb_device* dev, devtable::DevInfo& info);

    libusb_context* ctx_ = nullptr;
    const char* error_ = "libusb not initialized";
};

}  // namespace usbprobe
