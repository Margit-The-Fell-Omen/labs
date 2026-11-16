#pragma once

// Типы и вспомогательные функции для прямого общения с BlueZ через D-Bus.
//
// ObjectManager.GetManagedObjects имеет сигнатуру a{oa{sa{sv}}}. QtDBus не
// умеет маршалить такую вложенность через QVariantMap, поэтому объявлены
// собственные типы и операторы QDBusArgument.

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

using BluezPropertyMap = QVariantMap;                    // a{sv}
using BluezInterfaceMap = QMap<QString, BluezPropertyMap>;  // a{sa{sv}}
using BluezManagedObjects = QMap<QDBusObjectPath, BluezInterfaceMap>;  // a{oa{sa{sv}}}

Q_DECLARE_METATYPE(BluezInterfaceMap)
Q_DECLARE_METATYPE(BluezManagedObjects)
Q_DECLARE_METATYPE(QList<QDBusObjectPath>)

QDBusArgument& operator<<(QDBusArgument& arg, const BluezInterfaceMap& map);
QDBusArgument& operator<<(QDBusArgument& arg, const BluezManagedObjects& map);
const QDBusArgument& operator>>(const QDBusArgument& arg, BluezInterfaceMap& map);
const QDBusArgument& operator>>(const QDBusArgument& arg, BluezManagedObjects& map);

namespace dbusutil {

void registerTypes();

// hci0 → /org/bluez/hci0; пустая строка → первый Adapter1 из ObjectManager.
QString findAdapterPath(const QString& adapterName = {}, QString* error = nullptr);

QString toString(const QVariant& value);
QString toObjectPathString(const QVariant& value);
QStringList toStringList(const QVariant& value);
QByteArray toByteArray(const QVariant& value);
quint16 toUInt16(const QVariant& value);
bool toBool(const QVariant& value);

bool callVoid(const QString& service,
              const QString& path,
              const QString& iface,
              const QString& method,
              const QVariantList& args,
              QString* error = nullptr);

QVariantMap getProperties(const QString& path,
                          const QString& iface,
                          QString* error = nullptr);

bool setProperty(const QString& path,
                 const QString& iface,
                 const QString& name,
                 const QVariant& value,
                 QString* error = nullptr);

}  // namespace dbusutil
