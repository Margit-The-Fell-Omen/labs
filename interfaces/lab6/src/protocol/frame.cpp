#include "protocol/frame.h"

#include "protocol/crc32.h"

#include <QCryptographicHash>
#include <QtEndian>

#include <cstring>

namespace proto {

namespace {

void setError(QString* error, const QString& text)
{
    if (error)
        *error = text;
}

class Writer {
public:
    explicit Writer(QByteArray& out) : out_(out) {}

    void u8(quint8 v) { out_.append(static_cast<char>(v)); }
    void u16(quint16 v)
    {
        const quint16 le = qToLittleEndian(v);
        out_.append(reinterpret_cast<const char*>(&le), sizeof(le));
    }
    void u32(quint32 v)
    {
        const quint32 le = qToLittleEndian(v);
        out_.append(reinterpret_cast<const char*>(&le), sizeof(le));
    }
    void u64(quint64 v)
    {
        const quint64 le = qToLittleEndian(v);
        out_.append(reinterpret_cast<const char*>(&le), sizeof(le));
    }
    void bytes(const QByteArray& v) { out_.append(v); }

private:
    QByteArray& out_;
};

class Reader {
public:
    explicit Reader(const QByteArray& in) : in_(in) {}

    bool u8(quint8& v)
    {
        if (pos_ + 1 > in_.size())
            return false;
        v = static_cast<quint8>(in_.at(pos_));
        ++pos_;
        return true;
    }
    bool u16(quint16& v)
    {
        if (pos_ + 2 > in_.size())
            return false;
        quint16 le;
        std::memcpy(&le, in_.constData() + pos_, 2);
        v = qFromLittleEndian(le);
        pos_ += 2;
        return true;
    }
    bool u32(quint32& v)
    {
        if (pos_ + 4 > in_.size())
            return false;
        quint32 le;
        std::memcpy(&le, in_.constData() + pos_, 4);
        v = qFromLittleEndian(le);
        pos_ += 4;
        return true;
    }
    bool u64(quint64& v)
    {
        if (pos_ + 8 > in_.size())
            return false;
        quint64 le;
        std::memcpy(&le, in_.constData() + pos_, 8);
        v = qFromLittleEndian(le);
        pos_ += 8;
        return true;
    }
    bool bytes(int n, QByteArray& v)
    {
        if (n < 0 || pos_ + n > in_.size())
            return false;
        v = in_.mid(pos_, n);
        pos_ += n;
        return true;
    }
    bool string(int n, QString& v)
    {
        QByteArray raw;
        if (!bytes(n, raw))
            return false;
        v = QString::fromUtf8(raw);
        return true;
    }
    int remaining() const { return static_cast<int>(in_.size() - pos_); }
    int pos() const { return static_cast<int>(pos_); }

private:
    const QByteArray& in_;
    qsizetype pos_ = 0;
};

}  // namespace

QByteArray encodeChunk(quint64 offset, const QByteArray& payload)
{
    QByteArray out;
    out.reserve(kChunkHeaderSize + payload.size());
    Writer w(out);
    w.u64(offset);
    w.u32(Crc32::compute(payload));
    w.bytes(payload);
    return out;
}

bool decodeChunk(const QByteArray& frame, Chunk& out, QString* error)
{
    if (frame.size() < kChunkHeaderSize) {
        setError(error, QStringLiteral("чанк короче заголовка (%1 < %2)")
                            .arg(frame.size())
                            .arg(kChunkHeaderSize));
        return false;
    }

    Reader r(frame);
    Chunk c;
    if (!r.u64(c.offset) || !r.u32(c.crc)) {
        setError(error, QStringLiteral("не удалось разобрать заголовок чанка"));
        return false;
    }
    c.payload = frame.mid(kChunkHeaderSize);
    if (Crc32::compute(c.payload) != c.crc) {
        setError(error, QStringLiteral("CRC32 чанка не совпадает"));
        return false;
    }
    out = c;
    return true;
}

QByteArray encodeControl(const ControlMessage& msg)
{
    QByteArray body;
    Writer b(body);
    b.bytes(msg.transferId);

    switch (msg.type) {
    case ControlType::Start: {
        const QByteArray name = msg.fileName.toUtf8();
        const int nameLen = qMin(name.size(), 65535);
        b.u64(msg.totalSize);
        b.u16(msg.chunkSize);
        b.u32(msg.crc);
        b.u16(static_cast<quint16>(nameLen));
        b.bytes(name.left(nameLen));
        break;
    }
    case ControlType::StartAck:
        b.u8(static_cast<quint8>(msg.status));
        b.u64(msg.offset);
        b.u16(msg.mtu);
        b.u16(msg.chunkSize);
        break;
    case ControlType::Ack:
        b.u64(msg.offset);
        b.u8(static_cast<quint8>(msg.status));
        break;
    case ControlType::Fin:
        b.u64(msg.totalSize);
        b.u32(msg.crc);
        break;
    case ControlType::FinAck:
        b.u8(static_cast<quint8>(msg.status));
        b.u32(msg.crc);
        break;
    case ControlType::Hello:
        break;
    case ControlType::Abort: {
        const QByteArray reason = msg.reason.toUtf8();
        const int reasonLen = qMin(reason.size(), 65535);
        b.u8(static_cast<quint8>(msg.status));
        b.u16(static_cast<quint16>(reasonLen));
        b.bytes(reason.left(reasonLen));
        break;
    }
    }

    QByteArray out;
    out.reserve(3 + body.size());
    Writer w(out);
    w.u8(static_cast<quint8>(msg.type));
    w.u16(static_cast<quint16>(body.size()));
    w.bytes(body);
    return out;
}

bool decodeControl(const QByteArray& frame, ControlMessage& out, QString* error)
{
    if (frame.size() < 3) {
        setError(error, QStringLiteral("управляющий кадр короче 3 байт"));
        return false;
    }

    Reader r(frame);
    quint8 type = 0;
    quint16 bodyLen = 0;
    if (!r.u8(type) || !r.u16(bodyLen)) {
        setError(error, QStringLiteral("не удалось разобрать заголовок"));
        return false;
    }
    if (bodyLen != frame.size() - 3) {
        setError(error, QStringLiteral("длина тела не совпадает с кадром (%1 != %2)")
                            .arg(bodyLen)
                            .arg(frame.size() - 3));
        return false;
    }

    ControlMessage m;
    m.type = static_cast<ControlType>(type);
    if (!r.bytes(kTransferIdSize, m.transferId)) {
        setError(error, QStringLiteral("нет transferId"));
        return false;
    }

    switch (m.type) {
    case ControlType::Start: {
        quint16 nameLen = 0;
        if (!r.u64(m.totalSize) || !r.u16(m.chunkSize) || !r.u32(m.crc)
            || !r.u16(nameLen)) {
            setError(error, QStringLiteral("битый START"));
            return false;
        }
        if (!r.string(nameLen, m.fileName)) {
            setError(error, QStringLiteral("битое имя файла в START"));
            return false;
        }
        break;
    }
    case ControlType::StartAck: {
        quint8 status = 0;
        if (!r.u8(status) || !r.u64(m.offset) || !r.u16(m.mtu) || !r.u16(m.chunkSize)) {
            setError(error, QStringLiteral("битый START_ACK"));
            return false;
        }
        m.status = static_cast<Status>(status);
        break;
    }
    case ControlType::Ack: {
        quint8 status = 0;
        if (!r.u64(m.offset) || !r.u8(status)) {
            setError(error, QStringLiteral("битый ACK"));
            return false;
        }
        m.status = static_cast<Status>(status);
        break;
    }
    case ControlType::Fin:
        if (!r.u64(m.totalSize) || !r.u32(m.crc)) {
            setError(error, QStringLiteral("битый FIN"));
            return false;
        }
        break;
    case ControlType::FinAck: {
        quint8 status = 0;
        if (!r.u8(status) || !r.u32(m.crc)) {
            setError(error, QStringLiteral("битый FIN_ACK"));
            return false;
        }
        m.status = static_cast<Status>(status);
        break;
    }
    case ControlType::Hello:
        break;
    case ControlType::Abort: {
        quint8 status = 0;
        quint16 reasonLen = 0;
        if (!r.u8(status) || !r.u16(reasonLen)) {
            setError(error, QStringLiteral("битый ABORT"));
            return false;
        }
        if (!r.string(reasonLen, m.reason)) {
            setError(error, QStringLiteral("битая причина ABORT"));
            return false;
        }
        m.status = static_cast<Status>(status);
        break;
    }
    default:
        setError(error, QStringLiteral("неизвестный тип управляющего сообщения %1").arg(type));
        return false;
    }

    if (r.remaining() != 0) {
        setError(error, QStringLiteral("лишние байты в управляющем сообщении"));
        return false;
    }

    out = m;
    return true;
}

QByteArray makeTransferId(const QString& canonicalPath, quint64 size, qint64 mtimeMs)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(canonicalPath.toUtf8());
    h.addData(QByteArray(1, '\0'));
    h.addData(QByteArray::number(size));
    h.addData(QByteArray(1, '\0'));
    h.addData(QByteArray::number(mtimeMs));
    return h.result().left(kTransferIdSize);
}

QString statusName(Status s)
{
    switch (s) {
    case Status::Ok:
        return QStringLiteral("OK");
    case Status::Resume:
        return QStringLiteral("RESUME");
    case Status::Complete:
        return QStringLiteral("COMPLETE");
    case Status::CrcMismatch:
        return QStringLiteral("CRC_MISMATCH");
    case Status::SizeMismatch:
        return QStringLiteral("SIZE_MISMATCH");
    case Status::Error:
        return QStringLiteral("ERROR");
    case Status::Aborted:
        return QStringLiteral("ABORTED");
    }
    return QStringLiteral("UNKNOWN");
}

}  // namespace proto
