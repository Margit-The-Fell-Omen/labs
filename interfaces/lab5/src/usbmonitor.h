#pragma once
// Монитор шины в отдельном потоке: события libudev + резервный опрос QTimer.

#include <QObject>
#include <QString>

#include "devtable.h"
#include "usbprobe.h"

class QTimer;
class QSocketNotifier;
struct udev;
struct udev_monitor;

class UsbMonitor : public QObject {
    Q_OBJECT
public:
    explicit UsbMonitor(int intervalMs, QObject* parent = nullptr);
    ~UsbMonitor() override;

    bool ok() const { return prober_.ok(); }
    QString errorText() const { return QString::fromUtf8(prober_.errorText()); }

public slots:
    void start();
    void stop();

signals:
    void snapshotReady(const devtable::Snapshot& snap, const devtable::Diff& diff);
    void failed(const QString& message);

private slots:
    void poll();
    void onUdevEvent();

private:
    void setupUdev();
    void teardownUdev();

    usbprobe::Prober prober_;
    devtable::Snapshot last_;
    int intervalMs_ = 1000;
    QTimer* timer_ = nullptr;

    udev* udev_ = nullptr;
    udev_monitor* udevMon_ = nullptr;
    QSocketNotifier* udevNotifier_ = nullptr;
};