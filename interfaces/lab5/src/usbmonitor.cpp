#include "usbmonitor.h"

#include <QMetaType>
#include <QTimer>

// Регистрируем типы для передачи через очередные (межпотоковые) сигналы Qt.
Q_DECLARE_METATYPE(devtable::Snapshot)
Q_DECLARE_METATYPE(devtable::Diff)
Q_DECLARE_METATYPE(devtable::DevInfo)

UsbMonitor::UsbMonitor(int intervalMs, QObject* parent)
    : QObject(parent), intervalMs_(intervalMs) {
    // Одноразовая регистрация метатипов для queued-соединений.
    static bool registered = [] {
        qRegisterMetaType<devtable::Snapshot>("devtable::Snapshot");
        qRegisterMetaType<devtable::Diff>("devtable::Diff");
        qRegisterMetaType<devtable::DevInfo>("devtable::DevInfo");
        return true;
    }();
    (void)registered;
}

void UsbMonitor::start() {
    // Таймер создаём здесь (в рабочем потоке), тогда и его слоты сработают
    // в том же потоке — избегаем блокировки GUI дающими libusb-операциями.
    if (!timer_) {
        timer_ = new QTimer(this);
        timer_->setInterval(intervalMs_);
        // connect timer -> poll с автоподдержкой стиля connect.
        connect(timer_, &QTimer::timeout, this, &UsbMonitor::poll);
    }
    timer_->start();
}

void UsbMonitor::stop() {
    if (timer_)
        timer_->stop();
}

void UsbMonitor::poll() {
    // Один цикл опроса: получить снимок, найти разницу, сообщить GUI.
    devtable::Snapshot next;
    if (!prober_.snapshot(next)) {
        emit failed(QString::fromUtf8("Не удалось получить список USB-устройств"));
        return;
    }

    devtable::Diff d = devtable::diff(last_, next);
    // Всегда сообщаем снимок (для перерисовки таблицы), а при изменениях — разницу.
    if (last_.empty() && next.size() > 0)
        d = devtable::diff(devtable::Snapshot(), next);  // первый раз — всё "новое"
    last_ = next;
    emit snapshotReady(next, d);
}