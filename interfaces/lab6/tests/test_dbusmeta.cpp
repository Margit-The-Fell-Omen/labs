#include <QtTest/QtTest>

#include <QDBusMessage>
#include <QDBusMetaType>

#include "ble/dbusutil.h"

class TestDbusMeta : public QObject {
    Q_OBJECT
private slots:
    void signatures()
    {
        dbusutil::registerTypes();
        QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType(qMetaTypeId<BluezInterfaceMap>()))),
                 QByteArrayLiteral("a{sa{sv}}"));
        QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType(qMetaTypeId<BluezManagedObjects>()))),
                 QByteArrayLiteral("a{oa{sa{sv}}}"));
        QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType(qMetaTypeId<QList<QDBusObjectPath>>()))),
                 QByteArrayLiteral("ao"));
    }

    void managedObjectsRoundTrip()
    {
        dbusutil::registerTypes();

        BluezPropertyMap serviceProps;
        serviceProps.insert(QStringLiteral("UUID"), QStringLiteral("6c1f..."));
        serviceProps.insert(QStringLiteral("Primary"), true);
        serviceProps.insert(QStringLiteral("Characteristics"),
                            QVariant::fromValue(QList<QDBusObjectPath>{
                                QDBusObjectPath(QStringLiteral("/org/lab6/ble/service0/char0"))}));

        BluezInterfaceMap ifaces;
        ifaces.insert(QStringLiteral("org.bluez.GattService1"), serviceProps);

        BluezManagedObjects objects;
        objects.insert(QDBusObjectPath(QStringLiteral("/org/lab6/ble/service0")), ifaces);

        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.lab6.test"), QStringLiteral("/"),
            QStringLiteral("org.lab6.Test"), QStringLiteral("Objects"));
        msg << QVariant::fromValue(objects);
        const BluezManagedObjects parsed =
            qdbus_cast<BluezManagedObjects>(msg.arguments().first());
        QCOMPARE(parsed.size(), 1);
        QVERIFY(parsed.contains(QDBusObjectPath(QStringLiteral("/org/lab6/ble/service0"))));
        const BluezPropertyMap p =
            parsed.value(QDBusObjectPath(QStringLiteral("/org/lab6/ble/service0")))
                .value(QStringLiteral("org.bluez.GattService1"));
        QCOMPARE(p.value(QStringLiteral("UUID")).toString(), QStringLiteral("6c1f..."));
        QCOMPARE(p.value(QStringLiteral("Primary")).toBool(), true);
    }
};

QTEST_MAIN(TestDbusMeta)
#include "test_dbusmeta.moc"
