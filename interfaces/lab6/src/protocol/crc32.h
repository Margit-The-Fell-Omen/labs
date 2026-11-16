#pragma once

// CRC-32 (IEEE 802.3, polynomial 0xEDB88320).
// Используется для проверки каждого чанка и всего файла целиком.

#include <QByteArray>
#include <QtGlobal>

class Crc32 {
public:
    Crc32();

    void reset();
    void update(const void* data, qsizetype len);
    void update(const QByteArray& data);
    quint32 value() const;

    static quint32 compute(const QByteArray& data);

private:
    quint32 crc_ = 0xFFFFFFFFu;
};
