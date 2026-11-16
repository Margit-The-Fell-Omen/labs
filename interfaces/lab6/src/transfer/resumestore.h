#pragma once

// Метаданные частично принятого файла (<final>.part.json).

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct TransferMeta {
    QByteArray transferId;
    QString fileName;
    quint64 totalSize = 0;
    quint16 chunkSize = 0;
    quint64 received = 0;
    quint32 crc = 0;
    bool completed = false;

    bool isValid() const
    {
        return transferId.size() == 16 && totalSize > 0 && received <= totalSize;
    }
};

class ResumeStore {
public:
    static bool load(const QString& path, TransferMeta& out);
    static bool save(const QString& path, const TransferMeta& meta);
    static void remove(const QString& path);
};
