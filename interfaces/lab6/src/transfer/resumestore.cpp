#include "transfer/resumestore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

bool ResumeStore::load(const QString& path, TransferMeta& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject o = doc.object();
    TransferMeta m;
    m.transferId = QByteArray::fromHex(o.value(QStringLiteral("transferId")).toString().toLatin1());
    m.fileName = o.value(QStringLiteral("fileName")).toString();
    m.totalSize = static_cast<quint64>(o.value(QStringLiteral("totalSize")).toVariant().toULongLong());
    m.chunkSize = static_cast<quint16>(o.value(QStringLiteral("chunkSize")).toInt());
    m.received = static_cast<quint64>(o.value(QStringLiteral("received")).toVariant().toULongLong());
    m.crc = static_cast<quint32>(o.value(QStringLiteral("crc")).toVariant().toUInt());
    m.completed = o.value(QStringLiteral("completed")).toBool(false);

    if (!m.isValid())
        return false;

    out = m;
    return true;
}

bool ResumeStore::save(const QString& path, const TransferMeta& meta)
{
    QJsonObject o;
    o.insert(QStringLiteral("transferId"), QString::fromLatin1(meta.transferId.toHex()));
    o.insert(QStringLiteral("fileName"), meta.fileName);
    o.insert(QStringLiteral("totalSize"), QString::number(meta.totalSize));
    o.insert(QStringLiteral("chunkSize"), static_cast<int>(meta.chunkSize));
    o.insert(QStringLiteral("received"), QString::number(meta.received));
    o.insert(QStringLiteral("crc"), QString::number(meta.crc));
    o.insert(QStringLiteral("completed"), meta.completed);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return f.commit();
}

void ResumeStore::remove(const QString& path)
{
    QFile::remove(path);
}
