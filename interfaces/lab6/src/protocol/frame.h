#pragma once

// Кастомный протокол поверх GATT.
//
// Data characteristic: один GATT-write = один чанк:
//   [u64 offset][u32 crc32(payload)][payload]
//
// Control characteristic: одно сообщение = один START/ACK/FIN и т.п.:
//   [u8 type][u16 bodyLen][body]
//
// Все числа little-endian.

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace proto {

inline constexpr int kChunkHeaderSize = 12;
inline constexpr int kTransferIdSize = 16;

enum class ControlType : quint8 {
    Start = 1,
    StartAck = 2,
    Ack = 3,
    Fin = 4,
    FinAck = 5,
    Abort = 6,
    Hello = 7,
};

enum class Status : quint8 {
    Ok = 0,
    Resume = 1,
    Complete = 2,
    CrcMismatch = 3,
    SizeMismatch = 4,
    Error = 5,
    Aborted = 6,
};

struct Chunk {
    quint64 offset = 0;
    quint32 crc = 0;
    QByteArray payload;
};

struct ControlMessage {
    ControlType type = ControlType::Abort;
    QByteArray transferId;
    quint64 totalSize = 0;
    quint64 offset = 0;
    quint16 chunkSize = 0;
    quint16 mtu = 0;
    quint32 crc = 0;
    Status status = Status::Error;
    QString fileName;
    QString reason;
};

QByteArray encodeChunk(quint64 offset, const QByteArray& payload);
bool decodeChunk(const QByteArray& frame, Chunk& out, QString* error = nullptr);

QByteArray encodeControl(const ControlMessage& msg);
bool decodeControl(const QByteArray& frame, ControlMessage& out, QString* error = nullptr);

// Детерминированный идентификатор передачи: одинаковый файл (путь+размер+mtime)
// даёт одинаковый id, поэтому resume возможен даже после перезапуска отправителя.
QByteArray makeTransferId(const QString& canonicalPath, quint64 size, qint64 mtimeMs);

QString statusName(Status s);

}  // namespace proto
