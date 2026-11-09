#include "usbprobe.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "sysfs.h"

namespace usbprobe {

namespace {
// Класс HID по спецификации USB.
constexpr int kUsbClassHid = 0x03;
}  // namespace

Prober::Prober() {
    // Инициализация libusb. Лог-уровень поднимаем в DEBUG только для отладки;
    // по умолчанию WARN (убирает лишний шум).
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
    if (ctx_) {
        // Корректное завершение: освобождаем контекст libusb.
        libusb_exit(ctx_);
        ctx_ = nullptr;
    }
}

void Prober::fillDevice(libusb_device* dev, devtable::DevInfo& info) {
    // Базовые числовые поля из дескриптора устройства.
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

    // Строковые дескрипторы: пытаемся прочитать по индексам через libusb.
    // Нужны права на usbfs (root или udev-правило); при ошибке откатываемся к sysfs.
    bool anyStringOk = false;
    const auto readStr = [&](uint8_t idx, std::string& out) -> bool {
        if (idx == 0)
            return true;  // индекс 0 отсутствует — не ошибка.
        // Открываем устройство на время чтения строкового дескриптора.
        libusb_device_handle* h = nullptr;
        if (libusb_open(dev, &h) != LIBUSB_SUCCESS)
            return false;
        char buf[256];
        int n = libusb_get_string_descriptor_ascii(h, idx,
                                                   reinterpret_cast<unsigned char*>(buf),
                                                   sizeof(buf));
        libusb_close(h);
        if (n < 0)
            return false;
        out.assign(buf, static_cast<size_t>(n));
        return true;
    };

    if (readStr(dd.iManufacturer, info.manufacturer)) {
        if (!info.manufacturer.empty()) anyStringOk = true;
    }
    if (readStr(dd.iProduct, info.product)) {
        if (!info.product.empty()) anyStringOk = true;
    }
    if (readStr(dd.iSerialNumber, info.serial)) {
        if (!info.serial.empty()) anyStringOk = true;
    }

    // Если libusb вернул пустые строки/ошибки (нет доступа к usbfs), а также
    // безусловно страхуемся sysfs-атрибутами для producer/product/serial.
    const std::string devPath = sysfs::findDevicePath(info.key.bus, info.key.addr);
    if (!devPath.empty())
        sysfs::fillStrings(devPath, info);
    else
        std::fprintf(stderr, "  [%u:%u] sysfs path not found\n", info.key.bus,
                     info.key.addr);
    (void)anyStringOk;  // (сохраняется для будущей диагностики)

    // Подсчёт endpoint'ов и определение HID: проходим все конфигурации.
    libusb_config_descriptor* cfg = nullptr;
    rc = libusb_get_active_config_descriptor(dev, &cfg);
    if (rc != LIBUSB_SUCCESS)
        rc = libusb_get_config_descriptor(dev, 0, &cfg);  // fallback на config 0
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
    // HID: само устройство имеет класс 0x03, либо имеет HID-интерфейс.
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

    libusb_free_device_list(list, 1);  // 1 = освободить ссылки на устройства
    error_ = "";
    return true;
}

}  // namespace usbprobe