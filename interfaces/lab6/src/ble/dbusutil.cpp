#include "ble/dbusutil.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>

#include <mutex>

QDBusArgument& operator<<(QDBusArgument& arg, const BluezInterfaceMap& map)
{
    arg.beginMap(qMetaTypeId<QString>(), qMetaTypeId<QVariantMap>());
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        arg.beginMapEntry();
        arg << it.key();
        arg << it.value();
        arg.endMapEntry();
    }
    arg.endMap();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, BluezInterfaceMap& map)
{
    arg.beginMap();
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QString key;
        QVariantMap value;
        arg >> key >> value;
        arg.endMapEntry();
        map.insert(key, value);
    }
    arg.endMap();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const BluezManagedObjects& map)
{
    arg.beginMap(qMetaTypeId<QDBusObjectPath>(), qMetaTypeId<BluezInterfaceMap>());
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        arg.beginMapEntry();
        arg << it.key();
        arg << it.value();
        arg.endMapEntry();
    }
    arg.endMap();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, BluezManagedObjects& map)
{
    arg.beginMap();
    while (!arg.atEnd()) {
        arg.beginMapEntry();
        QDBusObjectPath key;
        BluezInterfaceMap value;
        arg >> key >> value;
        arg.endMapEntry();
        map.insert(key, value);
    }
    arg.endMap();
    return arg;
}

namespace dbusutil {

void registerTypes()
{
    static std::once_flag once;
    std::call_once(once, [] {
        qDBusRegisterMetaType<BluezInterfaceMap>();
        qDBusRegisterMetaType<BluezManagedObjects>();
        qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    });
}

QString findAdapterPath(const QString& adapterName, QString* error)
{
    registerTypes();

    if (!adapterName.isEmpty()) {
        if (adapterName.startsWith(QLatin1Char('/')))
            return adapterName;
        return QStringLiteral("/org/bluez/") + adapterName;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));

    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        const BluezManagedObjects objects = qdbus_cast<BluezManagedObjects>(reply.arguments().first());
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            if (it.value().contains(QStringLiteral("org.bluez.Adapter1")))
                return it.key().path();
        }
    }

    // На некоторых системах ObjectManager доступен только на /org/bluez.
    QDBusMessage msg2 = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        QStringLiteral("/org/bluez"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply2 = QDBusConnection::systemBus().call(msg2, QDBus::Block, 3000);
    if (reply2.type() == QDBusMessage::ReplyMessage && !reply2.arguments().isEmpty()) {
        const BluezManagedObjects objects = qdbus_cast<BluezManagedObjects>(reply2.arguments().first());
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            if (it.value().contains(QStringLiteral("org.bluez.Adapter1")))
                return it.key().path();
        }
    }

    if (error)
        *error = QStringLiteral("не найден Bluetooth-адаптер org.bluez.Adapter1");
    return {};
}

static QVariant unwrap(const QVariant& value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

QString toString(const QVariant& value)
{
    const QVariant v = unwrap(value);
    return v.toString();
}

QString toObjectPathString(const QVariant& value)
{
    const QVariant v = unwrap(value);
    if (v.userType() == qMetaTypeId<QDBusObjectPath>())
        return v.value<QDBusObjectPath>().path();
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        QDBusObjectPath p;
        const QDBusArgument arg = v.value<QDBusArgument>();
        arg >> p;
        return p.path();
    }
    return v.toString();
}

QStringList toStringList(const QVariant& value)
{
    const QVariant v = unwrap(value);
    if (v.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QStringList>(v);
    return v.toStringList();
}

QByteArray toByteArray(const QVariant& value)
{
    const QVariant v = unwrap(value);
    if (v.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        QByteArray out;
        arg >> out;
        return out;
    }
    return v.toByteArray();
}

quint16 toUInt16(const QVariant& value)
{
    const QVariant v = unwrap(value);
    return static_cast<quint16>(v.toUInt());
}

bool toBool(const QVariant& value)
{
    return unwrap(value).toBool();
}

bool callVoid(const QString& service,
              const QString& path,
              const QString& iface,
              const QString& method,
              const QVariantList& args,
              QString* error)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, iface, method);
    for (const QVariant& a : args)
        msg << a;

    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(reply.errorName(), reply.errorMessage());
        return false;
    }
    return true;
}

QVariantMap getProperties(const QString& path, const QString& iface, QString* error)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        path,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    msg << iface;

    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(reply.errorName(), reply.errorMessage());
        return {};
    }
    if (reply.arguments().isEmpty())
        return {};
    return qdbus_cast<QVariantMap>(reply.arguments().first());
}

bool setProperty(const QString& path,
                 const QString& iface,
                 const QString& name,
                 const QVariant& value,
                 QString* error)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"),
        path,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Set"));
    msg << iface << name << QVariant::fromValue(QDBusVariant(value));

    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(reply.errorName(), reply.errorMessage());
        return false;
    }
    return true;
}

}  // namespace dbusutil
