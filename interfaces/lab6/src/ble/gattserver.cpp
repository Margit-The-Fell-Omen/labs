#include "ble/gattserver.h"

#include "ble/bluezconst.h"
#include "protocol/frame.h"
#include "transfer/receiver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>

GattServer::GattServer(Receiver* receiver, QObject* parent)
    : QDBusVirtualObject(parent), receiver_(receiver)
{
    dbusutil::registerTypes();
    if (receiver_)
        connect(receiver_, &Receiver::controlToSend, this, &GattServer::sendControlNotification);
}

GattServer::~GattServer()
{
    stop();
}

QString GattServer::stateText() const
{
    if (advertising_)
        return QStringLiteral("peripheral: реклама включена");
    if (registered_)
        return QStringLiteral("peripheral: GATT зарегистрирован, реклама выключена");
    return QStringLiteral("peripheral: остановлен");
}

namespace {
void sendNoBlock(const QString& path, const QString& iface, const QString& method,
                 const QVariantList& args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path, iface, method);
    for (const QVariant& a : args)
        msg << a;
    QDBusConnection::systemBus().call(msg, QDBus::NoBlock);
}
}  // namespace

bool GattServer::start(QString* error)
{
    dbusutil::registerTypes();

    if (!QDBusConnection::systemBus().isConnected()) {
        if (error)
            *error = QStringLiteral("нет соединения с системной шиной D-Bus");
        return false;
    }

    if (advertising_ || starting_)
        return true;

    if (!exported_) {
        QString err;
        adapterPath_ = dbusutil::findAdapterPath(adapterName_, &err);
        if (adapterPath_.isEmpty()) {
            if (error)
                *error = err;
            return false;
        }

        const QVariantMap adapterProps =
            dbusutil::getProperties(adapterPath_, QStringLiteral("org.bluez.Adapter1"), &err);
        if (!adapterProps.isEmpty() && !dbusutil::toBool(adapterProps.value(QStringLiteral("Powered")))) {
            if (error)
                *error = QStringLiteral("Bluetooth-адаптер %1 выключен").arg(adapterPath_);
            return false;
        }

        // На случай предыдущего аварийного запуска.
        sendNoBlock(adapterPath_, QStringLiteral("org.bluez.LEAdvertisingManager1"),
                    QStringLiteral("UnregisterAdvertisement"),
                    {QVariant::fromValue(QDBusObjectPath(bleconst::advertPath()))});
        sendNoBlock(adapterPath_, QStringLiteral("org.bluez.GattManager1"),
                    QStringLiteral("UnregisterApplication"),
                    {QVariant::fromValue(QDBusObjectPath(bleconst::appPath()))});

        if (!QDBusConnection::systemBus().registerVirtualObject(
                bleconst::appPath(), this, QDBusConnection::SubPath)) {
            if (error)
                *error = QStringLiteral("не удалось экспортировать GATT-объекты в D-Bus");
            return false;
        }
        exported_ = true;
    }

    if (registered_) {
        registerAdvertisementAsync();
        return true;
    }

    starting_ = true;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), adapterPath_,
        QStringLiteral("org.bluez.GattManager1"), QStringLiteral("RegisterApplication"));
    msg << QVariant::fromValue(QDBusObjectPath(bleconst::appPath()));
    msg << QVariantMap();

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        if (w->isError()) {
            starting_ = false;
            registered_ = false;
            emit logMessage(QStringLiteral("RegisterApplication: %1").arg(w->error().message()));
            emit advertisingChanged(false);
        } else {
            registered_ = true;
            emit logMessage(QStringLiteral("GATT application зарегистрирован: %1")
                                .arg(bleconst::appPath()));
            registerAdvertisementAsync();
        }
        w->deleteLater();
    });
    return true;
}

void GattServer::registerAdvertisementAsync()
{
    if (advertising_ || adapterPath_.isEmpty()) {
        starting_ = false;
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), adapterPath_,
        QStringLiteral("org.bluez.LEAdvertisingManager1"),
        QStringLiteral("RegisterAdvertisement"));
    msg << QVariant::fromValue(QDBusObjectPath(bleconst::advertPath()));
    msg << QVariantMap();

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        starting_ = false;
        if (w->isError()) {
            emit logMessage(QStringLiteral("RegisterAdvertisement: %1").arg(w->error().message()));
            emit advertisingChanged(false);
        } else {
            advertising_ = true;
            emit advertisingChanged(true);
            emit logMessage(QStringLiteral("LE advertising запущен, имя: %1")
                                .arg(bleconst::localName()));
        }
        w->deleteLater();
    });
}

void GattServer::stop()
{
    starting_ = false;
    if (!adapterPath_.isEmpty()) {
        sendNoBlock(adapterPath_, QStringLiteral("org.bluez.LEAdvertisingManager1"),
                    QStringLiteral("UnregisterAdvertisement"),
                    {QVariant::fromValue(QDBusObjectPath(bleconst::advertPath()))});
        sendNoBlock(adapterPath_, QStringLiteral("org.bluez.GattManager1"),
                    QStringLiteral("UnregisterApplication"),
                    {QVariant::fromValue(QDBusObjectPath(bleconst::appPath()))});
    }
    if (exported_) {
        QDBusConnection::systemBus().unregisterObject(bleconst::appPath());
        exported_ = false;
    }

    const bool wasAdvertising = advertising_;
    advertising_ = false;
    registered_ = false;
    if (wasAdvertising)
        emit advertisingChanged(false);

    if (receiver_)
        receiver_->setNotifying(false);
    controlNotifying_ = false;
    clientSeen_ = false;
    controlValue_.clear();
}

void GattServer::sendControlNotification(const QByteArray& value)
{
    controlValue_ = value;

    if (!advertising_) {
        emit logMessage(QStringLiteral("ACK не отправлен: advertising выключен"));
        return;
    }
    if (!controlNotifying_) {
        emit logMessage(QStringLiteral("ACK не отправлен: central не подписан на Control"));
        return;
    }

    QVariantMap changed;
    changed.insert(QStringLiteral("Value"), QVariant::fromValue(value));

    QDBusMessage signal = QDBusMessage::createSignal(
        bleconst::controlPath(),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.bluez.GattCharacteristic1");
    signal << changed;
    signal << QStringList();

    QDBusConnection::systemBus().send(signal);
}

BluezPropertyMap GattServer::serviceProperties() const
{
    BluezPropertyMap p;
    p.insert(QStringLiteral("UUID"), bleconst::serviceUuid());
    p.insert(QStringLiteral("Primary"), true);
    p.insert(QStringLiteral("Characteristics"),
             QVariant::fromValue(QList<QDBusObjectPath>{
                 QDBusObjectPath(bleconst::dataPath()),
                 QDBusObjectPath(bleconst::controlPath())}));
    return p;
}

BluezPropertyMap GattServer::dataProperties() const
{
    BluezPropertyMap p;
    p.insert(QStringLiteral("UUID"), bleconst::dataUuid());
    p.insert(QStringLiteral("Service"), QVariant::fromValue(QDBusObjectPath(bleconst::servicePath())));
    p.insert(QStringLiteral("Flags"),
             QStringList{QStringLiteral("write-without-response"), QStringLiteral("write")});
    p.insert(QStringLiteral("Value"), QByteArray());
    return p;
}

BluezPropertyMap GattServer::controlProperties() const
{
    BluezPropertyMap p;
    p.insert(QStringLiteral("UUID"), bleconst::controlUuid());
    p.insert(QStringLiteral("Service"), QVariant::fromValue(QDBusObjectPath(bleconst::servicePath())));
    p.insert(QStringLiteral("Flags"),
             QStringList{QStringLiteral("read"), QStringLiteral("write"), QStringLiteral("notify")});
    p.insert(QStringLiteral("Value"), controlValue_);
    p.insert(QStringLiteral("Notifying"), controlNotifying_);
    return p;
}

BluezPropertyMap GattServer::advertProperties() const
{
    BluezPropertyMap p;
    p.insert(QStringLiteral("Type"), QStringLiteral("peripheral"));
    p.insert(QStringLiteral("ServiceUUIDs"), QStringList{bleconst::serviceUuid()});
    p.insert(QStringLiteral("LocalName"), bleconst::localName());
    return p;
}

BluezPropertyMap GattServer::propertiesFor(const QString& path, const QString& iface) const
{
    if (path == bleconst::servicePath() && iface == QStringLiteral("org.bluez.GattService1"))
        return serviceProperties();
    if (path == bleconst::dataPath() && iface == QStringLiteral("org.bluez.GattCharacteristic1"))
        return dataProperties();
    if (path == bleconst::controlPath() && iface == QStringLiteral("org.bluez.GattCharacteristic1"))
        return controlProperties();
    if (path == bleconst::advertPath() && iface == QStringLiteral("org.bluez.LEAdvertisement1"))
        return advertProperties();
    return {};
}

BluezManagedObjects GattServer::managedObjects() const
{
    BluezManagedObjects objects;

    {
        BluezInterfaceMap m;
        m.insert(QStringLiteral("org.bluez.GattService1"), serviceProperties());
        objects.insert(QDBusObjectPath(bleconst::servicePath()), m);
    }
    {
        BluezInterfaceMap m;
        m.insert(QStringLiteral("org.bluez.GattCharacteristic1"), dataProperties());
        objects.insert(QDBusObjectPath(bleconst::dataPath()), m);
    }
    {
        BluezInterfaceMap m;
        m.insert(QStringLiteral("org.bluez.GattCharacteristic1"), controlProperties());
        objects.insert(QDBusObjectPath(bleconst::controlPath()), m);
    }
    return objects;
}

void GattServer::sendReply(const QDBusConnection& conn, const QDBusMessage& msg,
                           const QVariantList& args)
{
    QDBusMessage reply = msg.createReply();
    reply.setArguments(args);
    conn.send(reply);
}

void GattServer::sendError(const QDBusConnection& conn, const QDBusMessage& msg,
                           const QString& name, const QString& text)
{
    conn.send(msg.createErrorReply(name, text));
}

void GattServer::handleProperties(const QDBusMessage& message, const QDBusConnection& conn)
{
    const QString path = message.path();
    const QVariantList args = message.arguments();

    if (message.member() == QStringLiteral("Get")) {
        if (args.size() < 2) {
            sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                      QStringLiteral("Get ожидает interface и property"));
            return;
        }
        const QString iface = args.at(0).toString();
        const QString prop = args.at(1).toString();
        const BluezPropertyMap props = propertiesFor(path, iface);
        if (!props.contains(prop)) {
            sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty"),
                      QStringLiteral("нет свойства %1.%2").arg(iface, prop));
            return;
        }
        sendReply(conn, message,
                  {QVariant::fromValue(QDBusVariant(props.value(prop)))});
        return;
    }

    if (message.member() == QStringLiteral("GetAll")) {
        const QString iface = args.isEmpty() ? QString() : args.at(0).toString();
        sendReply(conn, message, {propertiesFor(path, iface)});
        return;
    }

    if (message.member() == QStringLiteral("Set")) {
        sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.PropertyReadOnly"),
                  QStringLiteral("свойства GATT-объектов доступны только для чтения"));
        return;
    }

    sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
              QStringLiteral("неизвестный метод Properties.%1").arg(message.member()));
}

void GattServer::handleWriteValue(const QDBusMessage& message, const QDBusConnection& conn)
{
    const QVariantList args = message.arguments();
    if (args.isEmpty()) {
        sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                  QStringLiteral("WriteValue без value"));
        return;
    }

    const QByteArray value = dbusutil::toByteArray(args.at(0));
    QVariantMap options;
    if (args.size() > 1) {
        const QVariant o = args.at(1);
        options = (o.userType() == qMetaTypeId<QDBusArgument>())
                      ? qdbus_cast<QVariantMap>(o)
                      : o.toMap();
    }

    if (!clientSeen_) {
        clientSeen_ = true;
        emit clientSeen();
        emit logMessage(QStringLiteral("central подключился и начал писать"));
    }

    if (receiver_) {
        const quint16 mtu = dbusutil::toUInt16(options.value(QStringLiteral("mtu")));
        if (mtu > 0)
            receiver_->setMtu(mtu);

        if (message.path() == bleconst::dataPath()) {
            QString err;
            if (!receiver_->handleChunk(value, &err))
                emit logMessage(QStringLiteral("chunk: %1").arg(err));
        } else if (message.path() == bleconst::controlPath()) {
            receiver_->handleControl(value);
        }
    }

    sendReply(conn, message);
}

void GattServer::handleCharacteristic(const QDBusMessage& message, const QDBusConnection& conn)
{
    const QString path = message.path();
    if (path != bleconst::dataPath() && path != bleconst::controlPath()) {
        sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"),
                  QStringLiteral("неизвестная характеристика %1").arg(path));
        return;
    }

    if (message.member() == QStringLiteral("ReadValue")) {
        const QByteArray value = (path == bleconst::controlPath()) ? controlValue_ : QByteArray();
        sendReply(conn, message, {value});
        return;
    }

    if (message.member() == QStringLiteral("WriteValue")) {
        handleWriteValue(message, conn);
        return;
    }

    if (message.member() == QStringLiteral("StartNotify")) {
        if (path != bleconst::controlPath()) {
            sendError(conn, message, QStringLiteral("org.bluez.Error.NotSupported"),
                      QStringLiteral("notify поддерживается только Control"));
            return;
        }
        controlNotifying_ = true;
        if (receiver_)
            receiver_->setNotifying(true);
        sendReply(conn, message);
        return;
    }

    if (message.member() == QStringLiteral("StopNotify")) {
        controlNotifying_ = false;
        if (receiver_)
            receiver_->setNotifying(false);
        sendReply(conn, message);
        emit clientGone();
        return;
    }

    if (message.member() == QStringLiteral("Confirm")) {
        sendReply(conn, message);
        return;
    }

    sendError(conn, message, QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
              QStringLiteral("неизвестный метод GattCharacteristic1.%1").arg(message.member()));
}

bool GattServer::handleMessage(const QDBusMessage& message, const QDBusConnection& connection)
{
    const QString path = message.path();
    const QString iface = message.interface();
    const QString member = message.member();

    if (iface == QStringLiteral("org.freedesktop.DBus.Introspectable")
        && member == QStringLiteral("Introspect")) {
        sendReply(connection, message, {introspectObject(path)});
        return true;
    }

    if (iface == QStringLiteral("org.freedesktop.DBus.Properties")) {
        handleProperties(message, connection);
        return true;
    }

    if (iface == QStringLiteral("org.freedesktop.DBus.ObjectManager")
        && member == QStringLiteral("GetManagedObjects")) {
        if (path != bleconst::appPath()) {
            sendError(connection, message, QStringLiteral("org.freedesktop.DBus.Error.UnknownObject"),
                      QStringLiteral("ObjectManager доступен только на %1").arg(bleconst::appPath()));
            return true;
        }
        sendReply(connection, message, {QVariant::fromValue(managedObjects())});
        return true;
    }

    if (iface == QStringLiteral("org.bluez.GattCharacteristic1")) {
        handleCharacteristic(message, connection);
        return true;
    }

    if (iface == QStringLiteral("org.bluez.LEAdvertisement1")
        && member == QStringLiteral("Release")) {
        sendReply(connection, message);
        emit logMessage(QStringLiteral("BlueZ вызвал LEAdvertisement1.Release"));
        return true;
    }

    sendError(connection, message, QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
              QStringLiteral("неизвестный вызов %1.%2 на %3").arg(iface, member, path));
    return true;
}

QString GattServer::introspectObject(const QString& path) const
{
    const QString propertiesIface = QStringLiteral(
        "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
        "    <method name=\"Get\">\n"
        "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n"
        "      <arg name=\"property\" type=\"s\" direction=\"in\"/>\n"
        "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"
        "    </method>\n"
        "    <method name=\"GetAll\">\n"
        "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n"
        "      <arg name=\"properties\" type=\"a{sv}\" direction=\"out\"/>\n"
        "    </method>\n"
        "    <method name=\"Set\">\n"
        "      <arg name=\"interface\" type=\"s\" direction=\"in\"/>\n"
        "      <arg name=\"property\" type=\"s\" direction=\"in\"/>\n"
        "      <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"
        "    </method>\n"
        "    <signal name=\"PropertiesChanged\">\n"
        "      <arg name=\"interface\" type=\"s\"/>\n"
        "      <arg name=\"changed\" type=\"a{sv}\"/>\n"
        "      <arg name=\"invalidated\" type=\"as\"/>\n"
        "    </signal>\n"
        "  </interface>\n");

    const QString introspectable = QStringLiteral(
        "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
        "    <method name=\"Introspect\">\n"
        "      <arg name=\"xml\" type=\"s\" direction=\"out\"/>\n"
        "    </method>\n"
        "  </interface>\n");

    if (path == bleconst::appPath()) {
        return QStringLiteral("<node>\n"
                              "  <interface name=\"org.freedesktop.DBus.ObjectManager\">\n"
                              "    <method name=\"GetManagedObjects\">\n"
                              "      <arg name=\"objects\" type=\"a{oa{sa{sv}}}\" direction=\"out\"/>\n"
                              "    </method>\n"
                              "  </interface>\n")
               + introspectable + QStringLiteral("</node>\n");
    }

    if (path == bleconst::servicePath()) {
        return QStringLiteral("<node>\n"
                              "  <interface name=\"org.bluez.GattService1\">\n"
                              "    <property name=\"UUID\" type=\"s\" access=\"read\"/>\n"
                              "    <property name=\"Primary\" type=\"b\" access=\"read\"/>\n"
                              "    <property name=\"Characteristics\" type=\"ao\" access=\"read\"/>\n"
                              "  </interface>\n")
               + propertiesIface + introspectable + QStringLiteral("</node>\n");
    }

    if (path == bleconst::dataPath() || path == bleconst::controlPath()) {
        return QStringLiteral("<node>\n"
                              "  <interface name=\"org.bluez.GattCharacteristic1\">\n"
                              "    <method name=\"ReadValue\">\n"
                              "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n"
                              "      <arg name=\"value\" type=\"ay\" direction=\"out\"/>\n"
                              "    </method>\n"
                              "    <method name=\"WriteValue\">\n"
                              "      <arg name=\"value\" type=\"ay\" direction=\"in\"/>\n"
                              "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n"
                              "    </method>\n"
                              "    <method name=\"StartNotify\"/>\n"
                              "    <method name=\"StopNotify\"/>\n"
                              "    <property name=\"UUID\" type=\"s\" access=\"read\"/>\n"
                              "    <property name=\"Service\" type=\"o\" access=\"read\"/>\n"
                              "    <property name=\"Flags\" type=\"as\" access=\"read\"/>\n"
                              "    <property name=\"Value\" type=\"ay\" access=\"read\"/>\n"
                              "  </interface>\n")
               + propertiesIface + introspectable + QStringLiteral("</node>\n");
    }

    if (path == bleconst::advertPath()) {
        return QStringLiteral("<node>\n"
                              "  <interface name=\"org.bluez.LEAdvertisement1\">\n"
                              "    <method name=\"Release\"/>\n"
                              "    <property name=\"Type\" type=\"s\" access=\"read\"/>\n"
                              "    <property name=\"ServiceUUIDs\" type=\"as\" access=\"read\"/>\n"
                              "    <property name=\"LocalName\" type=\"s\" access=\"read\"/>\n"
                              "  </interface>\n")
               + propertiesIface + introspectable + QStringLiteral("</node>\n");
    }

    return QStringLiteral("<node/>\n");
}

QString GattServer::introspect(const QString& path) const
{
    return introspectObject(path);
}
