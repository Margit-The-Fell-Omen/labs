#include "usbmonitor.h"

#include <libudev.h>

#include <QMetaType>
#include <QSocketNotifier>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <cstdlib>

// Метатипы для межпотоковых (queued) сигналов.
Q_DECLARE_METATYPE(devtable::Snapshot)
Q_DECLARE_METATYPE(devtable::Diff)
Q_DECLARE_METATYPE(devtable::DevInfo)

UsbMonitor::UsbMonitor(int intervalMs, QObject* parent)
    : QObject(parent), intervalMs_(intervalMs) {
    static bool registered = [] {
        qRegisterMetaType<devtable::Snapshot>("devtable::Snapshot");
        qRegisterMetaType<devtable::Diff>("devtable::Diff");
        qRegisterMetaType<devtable::DevInfo>("devtable::DevInfo");
        return true;
    }();
    (void)registered;
}

UsbMonitor::~UsbMonitor() {
    teardownUdev();
}

void UsbMonitor::setupUdev() {
    if (udev_)
        return;
    udev_ = udev_new();
    if (!udev_) {
        std::fprintf(stderr, "udev_new() failed: события через netlink недоступны\n");
        return;
    }
    udevMon_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (!udevMon_) {
        std::fprintf(stderr, "udev_monitor_new_from_netlink() failed\n");
        return;
    }
    udev_monitor_filter_add_match_subsystem_devtype(udevMon_, "usb", "usb_device");
    if (udev_monitor_enable_receiving(udevMon_) < 0) {
        std::fprintf(stderr, "udev_monitor_enable_receiving() failed\n");
        return;
    }
    const int fd = udev_monitor_get_fd(udevMon_);
    if (fd < 0) {
        std::fprintf(stderr, "udev_monitor_get_fd() failed\n");
        return;
    }
    udevNotifier_ = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(udevNotifier_, &QSocketNotifier::activated, this,
            &UsbMonitor::onUdevEvent);
}

void UsbMonitor::teardownUdev() {
    if (udevNotifier_) {
        delete udevNotifier_;
        udevNotifier_ = nullptr;
    }
    if (udevMon_) {
        udev_monitor_unref(udevMon_);
        udevMon_ = nullptr;
    }
    if (udev_) {
        udev_unref(udev_);
        udev_ = nullptr;
    }
}

void UsbMonitor::start() {
    poll();
    setupUdev();
    if (!timer_) {
        timer_ = new QTimer(this);
        timer_->setInterval(intervalMs_);
        connect(timer_, &QTimer::timeout, this, &UsbMonitor::poll);
    }
    timer_->start();
}

void UsbMonitor::stop() {
    if (timer_)
        timer_->stop();
    teardownUdev();
}

void UsbMonitor::onUdevEvent() {
    if (!udevMon_)
        return;
    while (udev_device* dev = udev_monitor_receive_device(udevMon_))
        udev_device_unref(dev);
    poll();
}

void UsbMonitor::poll() {
    devtable::Snapshot next;
    if (!prober_.snapshot(next)) {
        emit failed(QString::fromUtf8("Не удалось получить список USB-устройств"));
        return;
    }

    devtable::Diff d = devtable::diff(last_, next);
    last_ = next;
    emit snapshotReady(next, d);

    // Диагностика: USBHIDPROBE_DEBUG=1.
    if (std::getenv("USBHIDPROBE_DEBUG")) {
        using namespace std::chrono;
        static auto prev = steady_clock::now();
        auto now = steady_clock::now();
        auto dt = duration_cast<milliseconds>(now - prev).count();
        prev = now;
        std::fprintf(stderr,
                     "[poll] interval=%lldms devices=%zu added=%zu removed=%zu\n",
                     static_cast<long long>(dt), next.size(), d.added.size(),
                     d.removed.size());
    }
}
