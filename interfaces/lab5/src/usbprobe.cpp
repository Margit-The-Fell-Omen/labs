#include "usbprobe.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "sysfs.h"

namespace usbprobe {

namespace {
// Класс HID по спецификации USB.
constexpr int kUsbClassHid = 0x03;
}  // namespace

Prober::Prober() {
    int rc = libusb_init(&ctx_);
    if (rc != LIBUSB_SUCCESS) {
        std::fprintf(stderr, "libusb_init() failed: %s\n",
                     libusb_error_name(rc));
        ctx_ = nullptr;
        error_ = "libusb_init() failed";
        return;
    }
#if defined(LIBUSB_OPTION_LOG_LEVEL)
    libusb_set_option(ctx_, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARN);
#endif
    error_ = "";
}

Prober::~Prober() {
    if (ctx_)
        libusb_exit(ctx_);
}

void Prober::fillDevice(libusb_device* dev, devtable::DevInfo& info) {
    info.key.bus = static_cast<uint8_t>(libusb_get_bus_number(dev));
    info.key.addr = static_cast<uint8_t>(libusb_get_device_address(dev));

    libusb_device_descriptor dd;
    int rc = libusb_get_device_descriptor(dev, &dd);
    if (rc != LIBUSB_SUCCESS) {
        std::fprintf(stderr, "  [%u:%u] get_device_descriptor: %s\n",
                     info.key.bus, info.key.addr, libusb_error_name(rc));
        return;
    }
    info.vid = dd.idVendor;
    info.pid = dd.idProduct;
    info.devClass = dd.bDeviceClass;
    info.devProtocol = dd.bDeviceProtocol;

    const std::string devPath = sysfs::findDevicePath(info.key.bus, info.key.addr);
    const bool devReady = !devPath.empty();
    if (devReady)
        sysfs::fillStrings(devPath, info);
    if (!devReady)
        return;

    // Подсчёт endpoint'ов и определение HID.
    libusb_config_descriptor* cfg = nullptr;
    rc = libusb_get_active_config_descriptor(dev, &cfg);
    if (rc != LIBUSB_SUCCESS)
        rc = libusb_get_config_descriptor(dev, 0, &cfg);
    if (rc != LIBUSB_SUCCESS) {
        std::fprintf(stderr, "  [%u:%u] config_descriptor: %s\n", info.key.bus,
                     info.key.addr, libusb_error_name(rc));
        return;
    }

    int eps = 0;
    bool ifaceHid = false;
    for (int iface = 0; iface < static_cast<int>(cfg->bNumInterfaces); ++iface) {
        const libusb_interface& it = cfg->interface[iface];
        for (int alt = 0; alt < static_cast<int>(it.num_altsetting); ++alt) {
            const libusb_interface_descriptor& ad = it.altsetting[alt];
            eps += static_cast<int>(ad.bNumEndpoints);
            if (ad.bInterfaceClass == kUsbClassHid)
                ifaceHid = true;
        }
    }
    info.endpointCount = eps;
    info.isHid = (info.devClass == kUsbClassHid) || ifaceHid;

    libusb_free_config_descriptor(cfg);
}

bool Prober::snapshot(devtable::Snapshot& out) {
    if (!ctx_)
        return false;

    out.clear();
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx_, &list);
    if (count < 0) {
        std::fprintf(stderr, "libusb_get_device_list: %s\n",
                     libusb_error_name(static_cast<int>(count)));
        error_ = "libusb_get_device_list() failed";
        return false;
    }

    for (ssize_t i = 0; i < count; ++i) {
        devtable::DevInfo info;
        fillDevice(list[i], info);
        out[info.key] = info;
    }

    libusb_free_device_list(list, 1);
    error_ = "";
    return true;
}

}  // namespace usbprobe
