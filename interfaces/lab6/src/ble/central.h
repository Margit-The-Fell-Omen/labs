#pragma once

// Central (отправитель): поиск через Adapter1, подключение через Device1,
// discovery GATT, StartNotify на Control, AcquireWrite на Data.
// AcquireWrite — быстрый путь (write-without-response на уровне BlueZ).
// Если AcquireWrite недоступен — fallback на WriteValue с type=command.

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

class QSocketNotifier;

struct BleDeviceInfo {
    QString path;
    QString address;
    QString name;
    int rssi = -127;
    bool hasService = false;
};

Q_DECLARE_METATYPE(BleDeviceInfo)

class BleCentral : public QObject {
    Q_OBJECT
public:
    explicit BleCentral(QObject* parent = nullptr);
    ~BleCentral() override;

    void setAdapter(const QString& adapterName);
    void startScan();
    void stopScan();
    bool isScanning() const { return scanning_; }

    QList<BleDeviceInfo> devices() const { return devices_; }
    void connectToDevice(const QString& path);
    void disconnectFromDevice();
    void reconnect();

    void sendControl(const QByteArray& message);
    bool sendChunk(const QByteArray& frame);
    bool canAcceptChunk() const;

    bool isReady() const { return ready_; }
    bool isConnected() const { return connected_; }
    quint16 mtu() const { return mtu_; }
    QString peerAddress() const { return peerAddress_; }
    QString stateText() const { return state_; }

signals:
    void devicesChanged(const QList<BleDeviceInfo>& devices);
    void stateChanged(const QString& text);
    void connected(const QString& address);
    void disconnected();
    void transportReady(quint16 mtu);
    void controlReceived(const QByteArray& value);
    void dataReceived(const QByteArray& value);
    void chunkWritable();
    void logMessage(const QString& text);
    void errorOccurred(const QString& text);

private slots:
    void refreshDevices();
    void onConnectPoll();
    void onWatchPoll();
    void onControlPropertiesChanged(const QString& iface,
                                    const QVariantMap& changed,
                                    const QStringList& invalidated);
    void onDataPropertiesChanged(const QString& iface,
                                 const QVariantMap& changed,
                                 const QStringList& invalidated);
    void flushWriteBuffer();

private:
    void ensureAdapter();
    void setupGatt();
    void acquireWrite();
    void startNotifications();
    void startNotifyFor(const QString& path, bool isControl);
    void sendHello();
    quint16 queryMtu();
    void closeWrite();
    void handleDisconnect(const QString& reason);
    void setState(const QString& text);
    void sendWriteValue(const QByteArray& value, bool withoutResponse);
    void registerControlSignal();
    void registerDataSignal();
    void unregisterSignals();

    QString adapterName_;
    QString adapterPath_;
    QString peerPath_;
    QString peerAddress_;
    QString servicePath_;
    QString dataPath_;
    QString controlPath_;

    QTimer scanTimer_;
    QTimer connectPollTimer_;
    QTimer connectTimeoutTimer_;
    QTimer watchTimer_;

    QList<BleDeviceInfo> devices_;
    bool scanning_ = false;
    bool connected_ = false;
    bool ready_ = false;
    bool useAcquireWrite_ = false;
    bool controlSignalConnected_ = false;
    bool dataSignalConnected_ = false;
    bool controlNotifyOk_ = false;
    bool dataNotifyOk_ = false;
    int pendingNotify_ = 0;
    bool loggedServicesWait_ = false;
    int watchErrors_ = 0;

    quint16 mtu_ = 23;
    int writeFd_ = -1;
    QSocketNotifier* writeNotifier_ = nullptr;
    QQueue<QByteArray> pendingFrames_;
    int outstandingWrites_ = 0;

    QString state_ = QStringLiteral("ожидание");
};
