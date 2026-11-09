#pragma once
// Фоновый опрос USB-шины в отдельном потоке Qt (QThread + QTimer).
// Периодически делает снимок через libusb, сравнивает с предыдущим и
// эмитит сигнал snapshotReady с новой таблицей и разницей (горячее подключение).

#include <QObject>
#include <QString>

#include "devtable.h"
#include "usbprobe.h"

class QTimer;

class UsbMonitor : public QObject {
    Q_OBJECT
public:
    // intervalMs — период опроса (мс). Создаётся в главном потоке, затем
    // переносится в рабочий QThread через moveToThread.
    explicit UsbMonitor(int intervalMs, QObject* parent = nullptr);

    bool ok() const { return prober_.ok(); }
    QString errorText() const { return QString::fromUtf8(prober_.errorText()); }

public slots:
    void start();  // запустить таймер опроса (вызывается из рабочего потока)
    void stop();   // остановить (вызывается перед завершением потока)

signals:
    void snapshotReady(const devtable::Snapshot& snap, const devtable::Diff& diff);
    void failed(const QString& message);

private slots:
    void poll();

private:
    usbprobe::Prober prober_;
    devtable::Snapshot last_;
    int intervalMs_ = 1000;
    QTimer* timer_ = nullptr;
};