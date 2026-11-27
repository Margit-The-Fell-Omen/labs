#include "ble/central.h"

#include "ble/bluezconst.h"
#include "ble/dbusutil.h"
#include "protocol/frame.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QSocketNotifier>

#include <algorithm>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kMaxOutstandingWrites = 16;
constexpr int kMaxPendingFrames = 64;
constexpr int kConnectTimeoutMs = 20000;

}  // namespace

BleCentral::BleCentral(QObject* parent) : QObject(parent)
{
    dbusutil::registerTypes();

    scanTimer_.setInterval(1000);
    connect(&scanTimer_, &QTimer::timeout, this, &BleCentral::refreshDevices);

    connectPollTimer_.setInterval(250);
    connect(&connectPollTimer_, &QTimer::timeout, this, &BleCentral::onConnectPoll);

    connectTimeoutTimer_.setSingleShot(true);
    connectTimeoutTimer_.setInterval(kConnectTimeoutMs);
    connect(&connectTimeoutTimer_, &QTimer::timeout, this, [this] {
        if (!connected_) {
            connectPollTimer_.stop();
            emit errorOccurred(QStringLiteral("таймаут подключения к %1").arg(peerAddress_));
            setState(QStringLiteral("таймаут подключения"));
        }
    });

    watchTimer_.setInterval(1000);
    connect(&watchTimer_, &QTimer::timeout, this, &BleCentral::onWatchPoll);

    controlPollTimer_.setInterval(200);
    connect(&controlPollTimer_, &QTimer::timeout, this, &BleCentral::onControlPoll);
}

BleCentral::~BleCentral()
{
    scanTimer_.stop();
    connectPollTimer_.stop();
    connectTimeoutTimer_.stop();
    watchTimer_.stop();
    controlPollTimer_.stop();
    if (connected_ || !peerPath_.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.bluez"), peerPath_,
            QStringLiteral("org.bluez.Device1"), QStringLiteral("Disconnect"));
        QDBusConnection::systemBus().call(msg, QDBus::NoBlock, 1000);
    }
    closeWrite();
}

void BleCentral::setAdapter(const QString& adapterName)
{
    adapterName_ = adapterName;
    adapterPath_.clear();
}

void BleCentral::ensureAdapter()
{
    if (!adapterPath_.isEmpty())
        return;
    QString err;
    adapterPath_ = dbusutil::findAdapterPath(adapterName_, &err);
    if (adapterPath_.isEmpty()) {
        emit errorOccurred(err);
        setState(QStringLiteral("адаптер не найден"));
    }
}

void BleCentral::startScan()
{
    if (scanning_)
        return;
    ensureAdapter();
    if (adapterPath_.isEmpty())
        return;

    QString err;
    const QVariantMap adapterProps =
        dbusutil::getProperties(adapterPath_, QStringLiteral("org.bluez.Adapter1"), &err);
    if (!adapterProps.isEmpty() && !dbusutil::toBool(adapterProps.value(QStringLiteral("Powered")))) {
        emit errorOccurred(QStringLiteral("Bluetooth-адаптер выключен"));
        return;
    }

    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    filter.insert(QStringLiteral("DuplicateData"), false);

    QString filterErr;
    if (!dbusutil::callVoid(QStringLiteral("org.bluez"), adapterPath_,
                            QStringLiteral("org.bluez.Adapter1"),
                            QStringLiteral("SetDiscoveryFilter"),
                            {filter}, &filterErr)) {
        // Некоторые адаптеры/версии не поддерживают фильтр — не фатально.
        emit logMessage(QStringLiteral("SetDiscoveryFilter: %1").arg(filterErr));
    }

    if (!dbusutil::callVoid(QStringLiteral("org.bluez"), adapterPath_,
                            QStringLiteral("org.bluez.Adapter1"),
                            QStringLiteral("StartDiscovery"), {}, &err)) {
        emit errorOccurred(QStringLiteral("StartDiscovery: %1").arg(err));
        return;
    }

    scanning_ = true;
    scanTimer_.start();
    setState(QStringLiteral("поиск BLE-устройств"));
    emit logMessage(QStringLiteral("поиск LE-устройств с сервисом %1").arg(bleconst::serviceUuid()));
    refreshDevices();
}

void BleCentral::stopScan()
{
    if (!scanning_)
        return;
    scanning_ = false;
    scanTimer_.stop();
    setState(QStringLiteral("поиск остановлен"));
    if (!adapterPath_.isEmpty()) {
        dbusutil::callVoid(QStringLiteral("org.bluez"), adapterPath_,
                           QStringLiteral("org.bluez.Adapter1"),
                           QStringLiteral("StopDiscovery"), {});
    }
}

void BleCentral::refreshDevices()
{
    ensureAdapter();
    if (adapterPath_.isEmpty())
        return;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        emit logMessage(QStringLiteral("GetManagedObjects: %1").arg(reply.errorMessage()));
        return;
    }
    if (reply.arguments().isEmpty())
        return;

    const BluezManagedObjects objects = qdbus_cast<BluezManagedObjects>(reply.arguments().first());
    QList<BleDeviceInfo> found;

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const BluezInterfaceMap& ifaces = it.value();
        if (!ifaces.contains(QStringLiteral("org.bluez.Device1")))
            continue;
        const BluezPropertyMap props = ifaces.value(QStringLiteral("org.bluez.Device1"));

        const QString adapter = dbusutil::toObjectPathString(props.value(QStringLiteral("Adapter")));
        if (!adapter.isEmpty() && adapter != adapterPath_)
            continue;
        if (adapter.isEmpty() && !it.key().path().startsWith(adapterPath_ + QStringLiteral("/dev_")))
            continue;

        BleDeviceInfo d;
        d.path = it.key().path();
        d.address = dbusutil::toString(props.value(QStringLiteral("Address")));
        d.name = dbusutil::toString(props.value(QStringLiteral("Name")));
        if (d.name.isEmpty())
            d.name = dbusutil::toString(props.value(QStringLiteral("Alias")));
        if (d.name.isEmpty())
            d.name = d.address;
        d.rssi = props.value(QStringLiteral("RSSI")).toInt();
        const QStringList uuids = dbusutil::toStringList(props.value(QStringLiteral("UUIDs")));
        d.hasService = uuids.contains(bleconst::serviceUuid(), Qt::CaseInsensitive);

        found.append(d);
    }

    std::sort(found.begin(), found.end(), [](const BleDeviceInfo& a, const BleDeviceInfo& b) {
        if (a.hasService != b.hasService)
            return a.hasService;
        if (a.rssi != b.rssi)
            return a.rssi > b.rssi;
        return a.address < b.address;
    });

    bool changed = found.size() != devices_.size();
    if (!changed) {
        for (int i = 0; i < found.size(); ++i) {
            const auto& a = found.at(i);
            const auto& b = devices_.at(i);
            if (a.path != b.path || a.address != b.address || a.name != b.name
                || a.rssi != b.rssi || a.hasService != b.hasService) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        devices_ = found;
        emit devicesChanged(devices_);
    }
}

bool BleCentral::forgetDevice(const QString& path, QString* error)
{
    ensureAdapter();
    if (adapterPath_.isEmpty()) {
        if (error)
            *error = QStringLiteral("нет Bluetooth-адаптера");
        return false;
    }
    if (path == peerPath_ && connected_)
        disconnectFromDevice();

    QString err;
    const bool ok = dbusutil::callVoid(
        QStringLiteral("org.bluez"), adapterPath_, QStringLiteral("org.bluez.Adapter1"),
        QStringLiteral("RemoveDevice"),
        {QVariant::fromValue(QDBusObjectPath(path))}, &err);
    if (!ok) {
        if (error)
            *error = err;
        return false;
    }
    emit logMessage(QStringLiteral("устройство удалено из BlueZ (кэш/bond): %1").arg(path));
    return true;
}

void BleCentral::connectToDevice(const QString& path)
{
    if (path.isEmpty())
        return;
    stopScan();
    unregisterSignals();
    closeWrite();
    connectPollTimer_.stop();
    connectTimeoutTimer_.stop();
    watchTimer_.stop();
    controlPollTimer_.stop();
    ready_ = false;
    connected_ = false;
    loggedServicesWait_ = false;
    connectPollTicks_ = 0;
    watchErrors_ = 0;
    peerPath_ = path;
    servicePath_.clear();
    dataPath_.clear();
    controlPath_.clear();

    QString err;
    const QVariantMap props =
        dbusutil::getProperties(peerPath_, QStringLiteral("org.bluez.Device1"), &err);
    peerAddress_ = dbusutil::toString(props.value(QStringLiteral("Address")));
    if (peerAddress_.isEmpty())
        peerAddress_ = peerPath_;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), peerPath_,
        QStringLiteral("org.bluez.Device1"), QStringLiteral("Connect"));
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg, 15000);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        if (w->isError()) {
            const QDBusError e = w->error();
            if (e.name() != QStringLiteral("org.bluez.Error.AlreadyConnected"))
                emit logMessage(QStringLiteral("Device1.Connect: %1").arg(e.message()));
        }
        w->deleteLater();
    });

    setState(QStringLiteral("подключение к %1").arg(peerAddress_));
    emit logMessage(QStringLiteral("Connect %1 (%2)").arg(peerAddress_, peerPath_));
    connectPollTimer_.start();
    connectTimeoutTimer_.start();
}

void BleCentral::reconnect()
{
    if (!peerPath_.isEmpty())
        connectToDevice(peerPath_);
}

void BleCentral::onConnectPoll()
{
    QString err;
    const QVariantMap props =
        dbusutil::getProperties(peerPath_, QStringLiteral("org.bluez.Device1"), &err);
    if (!err.isEmpty()) {
        emit logMessage(QStringLiteral("опрос Device1: %1").arg(err));
        return;
    }

    const bool isConnected = dbusutil::toBool(props.value(QStringLiteral("Connected")));
    const bool resolved = dbusutil::toBool(props.value(QStringLiteral("ServicesResolved")));

    if (isConnected) {
        ++connectPollTicks_;
        // Иногда BlueZ не поднимает ServicesResolved (кэш/гонка). Через ~3с
        // пробуем GetManagedObjects сами, не дожидаясь 20-секундного таймаута.
        if (resolved || connectPollTicks_ >= 12) {
            connectPollTimer_.stop();
            connectTimeoutTimer_.stop();
            connected_ = true;
            watchErrors_ = 0;
            setState(QStringLiteral("GATT discovery"));
            emit connected(peerAddress_);
            if (resolved)
                emit logMessage(QStringLiteral("connected, ServicesResolved=true"));
            else
                emit logMessage(QStringLiteral(
                    "connected, ServicesResolved=false — пробуем GATT discovery"));
            setupGatt();
            watchTimer_.start();
        } else if (!loggedServicesWait_) {
            loggedServicesWait_ = true;
            setState(QStringLiteral("ожидание ServicesResolved"));
            emit logMessage(QStringLiteral("connected, ждём ServicesResolved"));
        }
    }
}

void BleCentral::setupGatt()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        emit errorOccurred(QStringLiteral("не удалось получить GATT-объекты: %1")
                               .arg(reply.errorMessage()));
        return;
    }

    const BluezManagedObjects objects = qdbus_cast<BluezManagedObjects>(reply.arguments().first());

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it.key().path().startsWith(peerPath_ + QStringLiteral("/")))
            continue;
        const BluezInterfaceMap& ifaces = it.value();
        if (!ifaces.contains(QStringLiteral("org.bluez.GattService1")))
            continue;
        const BluezPropertyMap props = ifaces.value(QStringLiteral("org.bluez.GattService1"));
        if (dbusutil::toString(props.value(QStringLiteral("UUID")))
                .compare(bleconst::serviceUuid(), Qt::CaseInsensitive) == 0) {
            servicePath_ = it.key().path();
            break;
        }
    }

    if (servicePath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("сервис %1 не найден на %2")
                               .arg(bleconst::serviceUuid(), peerAddress_));
        return;
    }

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it.key().path().startsWith(servicePath_ + QStringLiteral("/")))
            continue;
        const BluezInterfaceMap& ifaces = it.value();
        if (!ifaces.contains(QStringLiteral("org.bluez.GattCharacteristic1")))
            continue;
        const BluezPropertyMap props = ifaces.value(QStringLiteral("org.bluez.GattCharacteristic1"));
        const QString uuid = dbusutil::toString(props.value(QStringLiteral("UUID")));
        if (uuid.compare(bleconst::dataUuid(), Qt::CaseInsensitive) == 0)
            dataPath_ = it.key().path();
        else if (uuid.compare(bleconst::controlUuid(), Qt::CaseInsensitive) == 0)
            controlPath_ = it.key().path();
    }

    if (dataPath_.isEmpty() || controlPath_.isEmpty()) {
        emit errorOccurred(QStringLiteral("не найдены Data/Control характеристики"));
        return;
    }

    emit logMessage(QStringLiteral("GATT: service=%1").arg(servicePath_));
    emit logMessage(QStringLiteral("GATT: data=%1").arg(dataPath_));
    emit logMessage(QStringLiteral("GATT: control=%1").arg(controlPath_));

    registerControlSignal();
    registerDataSignal();
    if (!controlSignalConnected_) {
        lastControlValue_.clear();
        controlPollTimer_.start();
    } else {
        controlPollTimer_.stop();
    }
    startNotifications();
}

void BleCentral::registerControlSignal()
{
    if (controlSignalConnected_)
        return;
    controlSignalConnected_ = QDBusConnection::systemBus().connect(
        QStringLiteral("org.bluez"),
        controlPath_,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(onControlPropertiesChanged(QString,QVariantMap,QStringList)));
    if (!controlSignalConnected_)
        emit logMessage(QStringLiteral("не удалось подписаться на PropertiesChanged Control"));
}

void BleCentral::registerDataSignal()
{
    if (dataSignalConnected_)
        return;
    dataSignalConnected_ = QDBusConnection::systemBus().connect(
        QStringLiteral("org.bluez"),
        dataPath_,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(onDataPropertiesChanged(QString,QVariantMap,QStringList)));
    if (!dataSignalConnected_)
        emit logMessage(QStringLiteral("не удалось подписаться на PropertiesChanged Data"));
}

void BleCentral::onControlPropertiesChanged(const QString& iface,
                                            const QVariantMap& changed,
                                            const QStringList& /*invalidated*/)
{
    if (iface != QStringLiteral("org.bluez.GattCharacteristic1"))
        return;
    if (!changed.contains(QStringLiteral("Value")))
        return;
    const QByteArray value = dbusutil::toByteArray(changed.value(QStringLiteral("Value")));
    if (value.isEmpty())
        return;
    proto::ControlMessage m;
    QString err;
    if (proto::decodeControl(value, m, &err))
        emit logMessage(QStringLiteral("← Control notify type=%1 len=%2")
                            .arg(static_cast<int>(m.type))
                            .arg(value.size()));
    else
        emit logMessage(QStringLiteral("← Control notify len=%1 (decode: %2)")
                            .arg(value.size())
                            .arg(err));
    emit controlReceived(value);
}

void BleCentral::onControlPoll()
{
    if (!connected_ || controlPath_.isEmpty())
        return;
    QString err;
    const QVariantMap props =
        dbusutil::getProperties(controlPath_, QStringLiteral("org.bluez.GattCharacteristic1"), &err);
    if (!err.isEmpty())
        return;
    const QByteArray value = dbusutil::toByteArray(props.value(QStringLiteral("Value")));
    if (value.isEmpty() || value == lastControlValue_)
        return;
    lastControlValue_ = value;
    proto::ControlMessage m;
    if (proto::decodeControl(value, m, &err))
        emit logMessage(QStringLiteral("← Control poll type=%1 len=%2")
                            .arg(static_cast<int>(m.type))
                            .arg(value.size()));
    emit controlReceived(value);
}

void BleCentral::onDataPropertiesChanged(const QString& iface,
                                         const QVariantMap& changed,
                                         const QStringList& /*invalidated*/)
{
    if (iface != QStringLiteral("org.bluez.GattCharacteristic1"))
        return;
    if (!changed.contains(QStringLiteral("Value")))
        return;
    const QByteArray value = dbusutil::toByteArray(changed.value(QStringLiteral("Value")));
    if (!value.isEmpty())
        emit dataReceived(value);
}

void BleCentral::startNotifications()
{
    controlNotifyOk_ = false;
    dataNotifyOk_ = false;
    startNotifyFor(controlPath_, true);
}

void BleCentral::startNotifyFor(const QString& path, bool isControl)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), path,
        QStringLiteral("org.bluez.GattCharacteristic1"), QStringLiteral("StartNotify"));
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, isControl](QDBusPendingCallWatcher* w) {
                if (w->isError()) {
                    emit logMessage(QStringLiteral("StartNotify(%1): %2")
                                        .arg(isControl ? QStringLiteral("Control")
                                                       : QStringLiteral("Data"),
                                             w->error().message()));
                } else if (isControl) {
                    controlNotifyOk_ = true;
                    emit logMessage(QStringLiteral("Control notifications включены"));
                } else {
                    dataNotifyOk_ = true;
                    emit logMessage(QStringLiteral("Data notifications включены"));
                }
                w->deleteLater();
                if (isControl) {
                    startNotifyFor(dataPath_, false);
                } else {
                    if (!controlNotifyOk_)
                        emit errorOccurred(QStringLiteral("Control notify недоступен"));
                    if (!dataNotifyOk_)
                        emit logMessage(QStringLiteral(
                            "Data notify недоступен: peripheral→central будет недоступен"));
                    acquireWrite();
                }
            });
}

void BleCentral::sendHello()
{
    proto::ControlMessage hello;
    hello.type = proto::ControlType::Hello;
    hello.transferId = QByteArray(proto::kTransferIdSize, '\0');
    sendWriteValue(proto::encodeControl(hello), false);
    emit logMessage(QStringLiteral("→ HELLO (central готов, MTU=%1)").arg(mtu_));
}

void BleCentral::acquireWrite()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), dataPath_,
        QStringLiteral("org.bluez.GattCharacteristic1"), QStringLiteral("AcquireWrite"));
    msg << QVariantMap();

    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        const QDBusPendingReply<QDBusUnixFileDescriptor, ushort> reply = *w;
        if (reply.isError()) {
            emit logMessage(QStringLiteral("AcquireWrite недоступен (%1), fallback WriteValue")
                                .arg(reply.error().message()));
            useAcquireWrite_ = false;
            mtu_ = queryMtu();
            ready_ = true;
            setState(QStringLiteral("готово (fallback WriteValue)"));
            emit transportReady(mtu_);
            sendHello();
        } else {
            const QDBusUnixFileDescriptor desc = reply.argumentAt<0>();
            const int rawFd = desc.fileDescriptor();
            const int fd = rawFd >= 0 ? ::dup(rawFd) : -1;
            if (fd < 0) {
                emit logMessage(QStringLiteral("dup(AcquireWrite fd) не удался, fallback"));
                useAcquireWrite_ = false;
                mtu_ = queryMtu();
                ready_ = true;
                setState(QStringLiteral("готово (fallback WriteValue)"));
                emit transportReady(mtu_);
                sendHello();
            } else {
                const int flags = ::fcntl(fd, F_GETFL, 0);
                ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
                writeFd_ = fd;
                writeNotifier_ = new QSocketNotifier(writeFd_, QSocketNotifier::Write, this);
                connect(writeNotifier_, &QSocketNotifier::activated,
                        this, &BleCentral::flushWriteBuffer);
                writeNotifier_->setEnabled(false);

                mtu_ = reply.argumentAt<1>();
                if (mtu_ == 0)
                    mtu_ = 23;
                useAcquireWrite_ = true;
                ready_ = true;
                setState(QStringLiteral("готово (AcquireWrite)"));
                emit logMessage(QStringLiteral("AcquireWrite: MTU=%1, payload=%2")
                                    .arg(mtu_)
                                    .arg(qMax<int>(1, mtu_ - 3 - 12)));
                emit transportReady(mtu_);
                sendHello();
            }
        }
        w->deleteLater();
    });
}

quint16 BleCentral::queryMtu()
{
    QString err;
    const QVariantMap props =
        dbusutil::getProperties(dataPath_, QStringLiteral("org.bluez.GattCharacteristic1"), &err);
    const quint16 mtu = dbusutil::toUInt16(props.value(QStringLiteral("MTU")));
    if (mtu >= 23)
        return mtu;
    return 23;
}

void BleCentral::sendControl(const QByteArray& message)
{
    if (!ready_ || controlPath_.isEmpty()) {
        emit logMessage(QStringLiteral("control не отправлен: транспорт не готов"));
        return;
    }
    proto::ControlMessage m;
    QString err;
    if (proto::decodeControl(message, m, &err))
        emit logMessage(QStringLiteral("→ Control write type=%1 len=%2")
                            .arg(static_cast<int>(m.type))
                            .arg(message.size()));
    else
        emit logMessage(QStringLiteral("→ Control write len=%1 (decode: %2)")
                            .arg(message.size())
                            .arg(err));
    sendWriteValue(message, false);
}

bool BleCentral::sendChunk(const QByteArray& frame)
{
    if (!ready_ || dataPath_.isEmpty() || frame.isEmpty())
        return false;

    if (useAcquireWrite_) {
        if (pendingFrames_.size() >= kMaxPendingFrames)
            return false;
        pendingFrames_.enqueue(frame);
        if (writeNotifier_)
            writeNotifier_->setEnabled(true);
        flushWriteBuffer();
        return true;
    }

    if (outstandingWrites_ >= kMaxOutstandingWrites)
        return false;
    sendWriteValue(frame, true);
    return true;
}

bool BleCentral::canAcceptChunk() const
{
    if (!ready_)
        return false;
    if (useAcquireWrite_)
        return pendingFrames_.size() < kMaxPendingFrames;
    return outstandingWrites_ < kMaxOutstandingWrites;
}

void BleCentral::sendWriteValue(const QByteArray& value, bool withoutResponse)
{
    const QString path = withoutResponse ? dataPath_ : controlPath_;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), path,
        QStringLiteral("org.bluez.GattCharacteristic1"), QStringLiteral("WriteValue"));

    QVariantMap options;
    options.insert(QStringLiteral("type"),
                   withoutResponse ? QStringLiteral("command") : QStringLiteral("request"));
    msg << value << options;

    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    if (withoutResponse)
        ++outstandingWrites_;

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, withoutResponse](QDBusPendingCallWatcher* w) {
                if (withoutResponse && outstandingWrites_ > 0)
                    --outstandingWrites_;
                if (w->isError()) {
                    emit logMessage(QStringLiteral("WriteValue(%1): %2")
                                        .arg(withoutResponse ? QStringLiteral("command")
                                                             : QStringLiteral("request"),
                                             w->error().message()));
                }
                w->deleteLater();
                if (withoutResponse)
                    emit chunkWritable();
            });
}

void BleCentral::flushWriteBuffer()
{
    if (writeFd_ < 0)
        return;

    while (!pendingFrames_.isEmpty()) {
        const QByteArray frame = pendingFrames_.head();
        const ssize_t n = ::send(writeFd_, frame.constData(), frame.size(), MSG_NOSIGNAL);
        if (n == frame.size()) {
            pendingFrames_.dequeue();
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;

        emit errorOccurred(QStringLiteral("ошибка записи в AcquireWrite fd: %1")
                               .arg(n < 0 ? QString::fromLocal8Bit(std::strerror(errno))
                                          : QStringLiteral("частичная запись %1/%2")
                                                .arg(n)
                                                .arg(frame.size())));
        closeWrite();
        ready_ = false;
        return;
    }

    if (writeNotifier_)
        writeNotifier_->setEnabled(!pendingFrames_.isEmpty());
    if (pendingFrames_.size() < kMaxPendingFrames)
        emit chunkWritable();
}

void BleCentral::onWatchPoll()
{
    if (!connected_)
        return;

    QString err;
    const QVariantMap props =
        dbusutil::getProperties(peerPath_, QStringLiteral("org.bluez.Device1"), &err);
    if (!err.isEmpty()) {
        ++watchErrors_;
        if (watchErrors_ >= 3)
            handleDisconnect(QStringLiteral("устройство пропало из BlueZ"));
        return;
    }
    watchErrors_ = 0;
    if (!dbusutil::toBool(props.value(QStringLiteral("Connected"))))
        handleDisconnect(QStringLiteral("Device1.Connected=false"));
}

void BleCentral::disconnectFromDevice()
{
    if (peerPath_.isEmpty()) {
        handleDisconnect(QStringLiteral("disconnect"));
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), peerPath_,
        QStringLiteral("org.bluez.Device1"), QStringLiteral("Disconnect"));
    QDBusConnection::systemBus().call(msg, QDBus::NoBlock, 2000);
    handleDisconnect(QStringLiteral("отключено пользователем"));
}

void BleCentral::handleDisconnect(const QString& reason)
{
    const bool wasConnected = connected_;
    connected_ = false;
    ready_ = false;
    watchTimer_.stop();
    connectPollTimer_.stop();
    connectTimeoutTimer_.stop();
    controlPollTimer_.stop();
    closeWrite();
    unregisterSignals();

    setState(QStringLiteral("отключено"));
    emit logMessage(QStringLiteral("disconnected: %1").arg(reason));
    if (wasConnected)
        emit disconnected();
}

void BleCentral::unregisterSignals()
{
    if (controlSignalConnected_ && !controlPath_.isEmpty()) {
        QDBusConnection::systemBus().disconnect(
            QStringLiteral("org.bluez"), controlPath_,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), this,
            SLOT(onControlPropertiesChanged(QString,QVariantMap,QStringList)));
        controlSignalConnected_ = false;
    }
    if (dataSignalConnected_ && !dataPath_.isEmpty()) {
        QDBusConnection::systemBus().disconnect(
            QStringLiteral("org.bluez"), dataPath_,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), this,
            SLOT(onDataPropertiesChanged(QString,QVariantMap,QStringList)));
        dataSignalConnected_ = false;
    }
}

void BleCentral::closeWrite()
{
    if (writeNotifier_) {
        writeNotifier_->setEnabled(false);
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
    if (writeFd_ >= 0) {
        ::close(writeFd_);
        writeFd_ = -1;
    }
    pendingFrames_.clear();
    outstandingWrites_ = 0;
}

void BleCentral::setState(const QString& text)
{
    if (state_ == text)
        return;
    state_ = text;
    emit stateChanged(state_);
}
