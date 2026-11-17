#pragma once

// GATT-сервер (Peripheral) через BlueZ D-Bus:
//   GattManager1.RegisterApplication + LEAdvertisingManager1.RegisterAdvertisement.
//
// Объекты приложения экспортируются одним QDBusVirtualObject с SubPath:
//   /org/lab6/ble                       — ObjectManager
//   /org/lab6/ble/service0              — GattService1
//   /org/lab6/ble/service0/char_data    — GattCharacteristic1 (write-without-response)
//   /org/lab6/ble/service0/char_ctrl    — GattCharacteristic1 (read/write/notify)
//   /org/lab6/ble/advert                — LEAdvertisement1

#include <QDBusVirtualObject>
#include <QPointer>
#include <QObject>

#include "ble/dbusutil.h"

class Receiver;
class Sender;

class GattServer : public QDBusVirtualObject {
    Q_OBJECT
public:
    explicit GattServer(Receiver* receiver, Sender* sender, QObject* parent = nullptr);
    ~GattServer() override;

    void setAdapter(const QString& adapterName) { adapterName_ = adapterName; }
    bool start(QString* error = nullptr);
    void stop();
    bool isAdvertising() const { return advertising_; }
    QString adapterPath() const { return adapterPath_; }
    QString stateText() const;

    void sendControlNotification(const QByteArray& value);
    void sendDataNotification(const QByteArray& value);
    bool isDataNotifying() const { return dataNotifying_; }
    quint16 peerMtu() const { return peerMtu_; }
    bool hasPeerMtu() const { return peerMtuKnown_; }

private:
    void registerAdvertisementAsync();

    QString introspect(const QString& path) const override;
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override;

signals:
    void logMessage(const QString& text);
    void advertisingChanged(bool on);
    void clientSeen();
    void clientGone();
    void mtuChanged(quint16 mtu);
    void dataNotifyChanged(bool on);
    void controlActivity();

private:
    BluezPropertyMap serviceProperties() const;
    BluezPropertyMap dataProperties() const;
    BluezPropertyMap controlProperties() const;
    BluezPropertyMap advertProperties() const;
    BluezPropertyMap propertiesFor(const QString& path, const QString& iface) const;
    BluezManagedObjects managedObjects() const;

    void sendReply(const QDBusConnection& conn, const QDBusMessage& msg,
                   const QVariantList& args = {});
    void sendError(const QDBusConnection& conn, const QDBusMessage& msg,
                   const QString& name, const QString& text);

    void handleProperties(const QDBusMessage& message, const QDBusConnection& conn);
    void handleCharacteristic(const QDBusMessage& message, const QDBusConnection& conn);
    void handleWriteValue(const QDBusMessage& message, const QDBusConnection& conn);
    QString introspectObject(const QString& path) const;

    QPointer<Receiver> receiver_;
    QPointer<Sender> sender_;
    QString adapterName_;
    QString adapterPath_;
    bool exported_ = false;
    bool registered_ = false;
    bool starting_ = false;
    bool advertising_ = false;
    bool controlNotifying_ = false;
    bool dataNotifying_ = false;
    bool clientSeen_ = false;
    QByteArray controlValue_;
    QByteArray dataValue_;
    quint16 peerMtu_ = 23;
    bool peerMtuKnown_ = false;
};
