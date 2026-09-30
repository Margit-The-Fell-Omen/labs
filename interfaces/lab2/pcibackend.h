#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

struct PciFunction
{
    quint32 bus = 0;
    quint32 device = 0;
    quint32 function = 0;
    quint16 vendorId = 0;
    quint16 deviceId = 0;
    quint8 revision = 0;
    quint8 headerType = 0;
    quint8 baseClass = 0;
    quint8 subClass = 0;
    quint8 progIf = 0;
    quint8 secondaryBus = 0;
    bool isBridge = false;
    QString deviceDescription;
    QByteArray config;
};

using PciDeviceHandle = void *;

QHash<quint32, QString> pciDeviceDescriptions();

PciDeviceHandle openPciDevice(QString &error);
void closePciDevice(PciDeviceHandle handle);

bool readConfigSpace(PciDeviceHandle handle, quint32 bus, quint32 device, quint32 function,
                     QByteArray &data, QString &error);

QVector<PciFunction> scanPciBus(PciDeviceHandle handle, quint32 bus,
                                const QHash<quint32, QString> &descriptions, QString &error);