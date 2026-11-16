#include "transfer/sender.h"

#include "protocol/crc32.h"

#include <QDateTime>
#include <QFileInfo>

namespace {
constexpr qint64 kCrcReadBlock = 1024 * 1024;
constexpr qint64 kStallTimeoutMs = 5000;
}  // namespace

Sender::Sender(QObject* parent) : QObject(parent) {}

Sender::~Sender()
{
    if (file_.isOpen())
        file_.close();
}

bool Sender::start(const QString& filePath, QString* error)
{
    // Новый файл: старую передачу молча закрываем.
    if (file_.isOpen())
        file_.close();
    state_ = State::Idle;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isReadable()) {
        if (error)
            *error = QStringLiteral("файл не найден или недоступен: %1").arg(filePath);
        return false;
    }

    file_.setFileName(fi.absoluteFilePath());
    if (!file_.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("не удалось открыть %1: %2")
                         .arg(fi.fileName(), file_.errorString());
        return false;
    }

    info_ = fi;
    totalSize_ = static_cast<quint64>(file_.size());
    if (file_.size() < 0) {
        file_.close();
        if (error)
            *error = QStringLiteral("не удалось определить размер файла");
        return false;
    }

    const QString canonical = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath()
                                                               : fi.canonicalFilePath();
    transferId_ = proto::makeTransferId(canonical, totalSize_,
                                        fi.lastModified().toMSecsSinceEpoch());

    fileCrc_ = computeFileCrc();
    if (file_.error() != QFileDevice::NoError) {
        const QString err = file_.errorString();
        file_.close();
        if (error)
            *error = QStringLiteral("ошибка чтения файла при подсчёте CRC32: %1").arg(err);
        return false;
    }

    acked_ = 0;
    next_ = 0;
    chunkIndex_ = 0;
    state_ = State::WaitingReady;
    rate_.reset(0);
    activity_.restart();

    emit logMessage(QStringLiteral("Файл: %1 (%2 байт), CRC32=%3")
                        .arg(info_.fileName())
                        .arg(totalSize_)
                        .arg(fileCrc_, 8, 16, QLatin1Char('0')));
    emit logMessage(QStringLiteral("transferId=%1")
                        .arg(QString::fromLatin1(transferId_.toHex())));
    emit stateChanged(stateText());
    emit progress(0, totalSize_);
    return true;
}

quint32 Sender::computeFileCrc()
{
    Crc32 crc;
    file_.seek(0);
    QByteArray buf;
    buf.resize(kCrcReadBlock);
    for (;;) {
        const qint64 n = file_.read(buf.data(), buf.size());
        if (n < 0)
            return 0;
        if (n == 0)
            break;
        crc.update(buf.constData(), n);
    }
    file_.seek(0);
    return crc.value();
}

void Sender::updateChunkSize(quint16 mtu)
{
    if (mtu == 0)
        mtu = 23;
    int payload = static_cast<int>(mtu) - 3 - proto::kChunkHeaderSize;
    if (payload < 1)
        payload = 1;
    if (chunkOverride_ > 0)
        payload = qMin(payload, static_cast<int>(chunkOverride_));
    chunk_ = static_cast<quint16>(qMin(payload, 4096));
}

void Sender::onTransportReady(quint16 mtu)
{
    if (state_ == State::Idle || state_ == State::Done || state_ == State::Error)
        return;

    updateChunkSize(mtu);
    state_ = State::WaitingStartAck;
    sendStart();
    noteActivity();
    emit stateChanged(stateText());
}

void Sender::sendStart()
{
    proto::ControlMessage m;
    m.type = proto::ControlType::Start;
    m.transferId = transferId_;
    m.totalSize = totalSize_;
    m.chunkSize = chunk_;
    m.crc = fileCrc_;
    m.fileName = info_.fileName();
    emit controlToSend(proto::encodeControl(m));
    emit logMessage(QStringLiteral("→ START: %1 байт, chunk=%2, MTU-payload=%3")
                        .arg(totalSize_)
                        .arg(chunk_)
                        .arg(chunk_ + proto::kChunkHeaderSize));
}

void Sender::sendFin()
{
    if (state_ != State::Sending)
        return;

    state_ = State::Finishing;
    proto::ControlMessage m;
    m.type = proto::ControlType::Fin;
    m.transferId = transferId_;
    m.totalSize = totalSize_;
    m.crc = fileCrc_;
    emit controlToSend(proto::encodeControl(m));
    noteActivity();
    emit logMessage(QStringLiteral("→ FIN: CRC32=%1").arg(fileCrc_, 8, 16, QLatin1Char('0')));
    emit stateChanged(stateText());
}

QByteArray Sender::takeChunk()
{
    if (state_ != State::Sending || !canSendMore())
        return {};

    const quint64 left = totalSize_ - next_;
    const int n = static_cast<int>(qMin<quint64>(chunk_, left));
    if (n <= 0)
        return {};

    if (!file_.seek(static_cast<qint64>(next_))) {
        fail(QStringLiteral("seek(%1) не удался: %2").arg(next_).arg(file_.errorString()));
        return {};
    }

    QByteArray payload = file_.read(n);
    if (payload.size() != n) {
        fail(QStringLiteral("недочитан файл: ожидалось %1, получено %2")
                 .arg(n)
                 .arg(payload.size()));
        return {};
    }

    QByteArray frame = proto::encodeChunk(next_, payload);

    // Отладка: портим уже посчитанный CRC, чтобы приёмник увидел битый чанк.
    if (corruptChunk_ == chunkIndex_) {
        if (frame.size() > proto::kChunkHeaderSize)
            frame[frame.size() - 1] = static_cast<char>(frame.back() ^ 0x5A);
        emit logMessage(QStringLiteral("ОТЛАДКА: намеренно испорчен чанк #%1")
                            .arg(chunkIndex_));
    }
    ++chunkIndex_;

    next_ += static_cast<quint64>(n);
    noteActivity();
    return frame;
}

bool Sender::canSendMore() const
{
    return state_ == State::Sending && next_ < totalSize_
           && (next_ - acked_) < window_;
}

void Sender::pump()
{
    if (state_ == State::Sending && next_ >= totalSize_ && acked_ >= totalSize_)
        sendFin();
}

void Sender::onControl(const QByteArray& frame)
{
    proto::ControlMessage m;
    QString err;
    if (!proto::decodeControl(frame, m, &err)) {
        emit logMessage(QStringLiteral("битый control-кадр: %1").arg(err));
        return;
    }
    if (m.transferId != transferId_) {
        emit logMessage(QStringLiteral("control для чужого transferId — игнор"));
        return;
    }

    switch (m.type) {
    case proto::ControlType::StartAck: {
        if (state_ != State::WaitingStartAck) {
            emit logMessage(QStringLiteral("START_ACK в состоянии %1 — игнор").arg(stateText()));
            return;
        }
        if (m.status == proto::Status::Complete) {
            acked_ = totalSize_;
            next_ = totalSize_;
            state_ = State::Done;
            rate_.update(totalSize_);
            emit progress(totalSize_, totalSize_);
            emit stateChanged(stateText());
            emit finished(true, QStringLiteral("файл уже принят получателем (CRC32 совпал)"));
            return;
        }
        if (m.status != proto::Status::Ok && m.status != proto::Status::Resume) {
            fail(QStringLiteral("получатель отклонил START: %1").arg(proto::statusName(m.status)));
            return;
        }
        if (m.offset > totalSize_) {
            fail(QStringLiteral("START_ACK offset %1 больше размера файла %2")
                     .arg(m.offset)
                     .arg(totalSize_));
            return;
        }

        acked_ = m.offset;
        next_ = m.offset;
        rate_.reset(m.offset);
        state_ = State::Sending;
        noteActivity();
        emit progress(acked_, totalSize_);
        emit logMessage(QStringLiteral("← START_ACK: offset=%1, mtu=%2, chunk=%3")
                            .arg(m.offset)
                            .arg(m.mtu)
                            .arg(m.chunkSize));
        emit stateChanged(stateText());
        pump();
        break;
    }
    case proto::ControlType::Ack: {
        if (state_ != State::Sending && state_ != State::Finishing)
            return;

        if (m.status == proto::Status::Error) {
            // Приёмник увидел разрыв и просит повторить с offset.
            if (m.offset > totalSize_) {
                emit logMessage(QStringLiteral("NACK с некорректным offset — игнор"));
                return;
            }
            const quint64 oldAck = acked_;
            acked_ = m.offset;
            next_ = m.offset;
            if (acked_ > oldAck)
                rate_.update(acked_);
            // Если NACK пришёл после FIN, возвращаемся к передаче данных.
            if (state_ == State::Finishing)
                state_ = State::Sending;
            noteActivity();
            emit progress(acked_, totalSize_);
            emit logMessage(QStringLiteral("← NACK: повтор с offset=%1").arg(m.offset));
            emit stateChanged(stateText());
            return;
        }

        if (m.status != proto::Status::Ok)
            return;
        if (m.offset > totalSize_ || m.offset <= acked_)
            return;

        acked_ = m.offset;
        rate_.update(acked_);
        noteActivity();
        emit progress(acked_, totalSize_);
        if (acked_ >= totalSize_ && next_ >= totalSize_)
            pump();
        break;
    }
    case proto::ControlType::FinAck: {
        if (state_ == State::Done || state_ == State::Error)
            return;
        if (m.status == proto::Status::Ok) {
            state_ = State::Done;
            rate_.update(totalSize_);
            emit progress(totalSize_, totalSize_);
            emit stateChanged(stateText());
            emit logMessage(QStringLiteral("← FIN_ACK: CRC32=%1")
                                .arg(m.crc, 8, 16, QLatin1Char('0')));
            emit finished(true, QStringLiteral("передача завершена, CRC32 совпал"));
        } else {
            fail(QStringLiteral("CRC32 не совпал (получатель: %1)")
                     .arg(proto::statusName(m.status)));
        }
        break;
    }
    case proto::ControlType::Abort: {
        fail(QStringLiteral("получатель прервал передачу: %1")
                 .arg(m.reason.isEmpty() ? proto::statusName(m.status) : m.reason));
        break;
    }
    default:
        break;
    }
}

void Sender::onTransportLost()
{
    if (state_ == State::Idle || state_ == State::Done || state_ == State::Error)
        return;

    state_ = State::WaitingStartAck;
    activity_.restart();
    emit logMessage(QStringLiteral("соединение потеряно; ждём переподключения"));
    emit stateChanged(stateText());
}

void Sender::abort(const QString& reason)
{
    if (state_ == State::Idle) {
        emit logMessage(QStringLiteral("передача не запущена"));
        return;
    }

    if (state_ == State::Done || state_ == State::Error) {
        if (file_.isOpen())
            file_.close();
        state_ = State::Idle;
        return;
    }

    if (!transferId_.isEmpty()) {
        proto::ControlMessage m;
        m.type = proto::ControlType::Abort;
        m.transferId = transferId_;
        m.status = proto::Status::Aborted;
        m.reason = reason.isEmpty() ? QStringLiteral("отменено отправителем") : reason;
        emit controlToSend(proto::encodeControl(m));
    }

    if (file_.isOpen())
        file_.close();
    state_ = State::Idle;
    emit stateChanged(stateText());
    emit finished(false, reason.isEmpty() ? QStringLiteral("передача отменена") : reason);
}

bool Sender::checkStall()
{
    if (state_ == State::Sending) {
        if (activity_.elapsed() >= kStallTimeoutMs) {
            next_ = acked_;
            activity_.restart();
            emit logMessage(QStringLiteral(
                "таймаут ACK: повторная отправка с offset=%1").arg(acked_));
            emit stateChanged(stateText());
        }
        return false;
    }

    if (state_ == State::WaitingStartAck || state_ == State::Finishing)
        return activity_.elapsed() >= 2 * kStallTimeoutMs;

    return false;
}

void Sender::fail(const QString& message)
{
    if (state_ == State::Error || state_ == State::Done)
        return;
    if (file_.isOpen())
        file_.close();
    state_ = State::Error;
    emit logMessage(QStringLiteral("ошибка: %1").arg(message));
    emit stateChanged(stateText());
    emit finished(false, message);
}

void Sender::noteActivity()
{
    activity_.restart();
}

void Sender::setWindow(quint64 bytes)
{
    window_ = qMax<quint64>(4096, bytes);
}

void Sender::setChunkOverride(quint16 bytes)
{
    chunkOverride_ = bytes;
}

bool Sender::isActive() const
{
    return state_ == State::WaitingReady || state_ == State::WaitingStartAck
           || state_ == State::Sending || state_ == State::Finishing;
}

bool Sender::isFinished() const
{
    return state_ == State::Done || state_ == State::Error;
}

QString Sender::stateText() const
{
    switch (state_) {
    case State::Idle:
        return QStringLiteral("ожидание");
    case State::WaitingReady:
        return QStringLiteral("файл готов, ждём BLE-соединение");
    case State::WaitingStartAck:
        return QStringLiteral("ожидание START_ACK");
    case State::Sending:
        return QStringLiteral("передача");
    case State::Finishing:
        return QStringLiteral("ожидание FIN_ACK (CRC32)");
    case State::Done:
        return QStringLiteral("завершено");
    case State::Error:
        return QStringLiteral("ошибка");
    }
    return QStringLiteral("неизвестно");
}
