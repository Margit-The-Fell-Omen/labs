#include "transfer/receiver.h"

#include "protocol/crc32.h"
#include "transfer/resumestore.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr quint64 kAckInterval = 8 * 1024;
constexpr qint64 kAckTimerMs = 50;
constexpr quint64 kMetaInterval = 1024 * 1024;
constexpr qint64 kMetaTimerMs = 1000;
constexpr qint64 kCrcReadBlock = 1024 * 1024;

bool crcOfFile(const QString& path, quint64 expectedSize, quint32& crcOut, QString& error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("не удалось открыть %1 для CRC32: %2")
                    .arg(path, f.errorString());
        return false;
    }
    if (static_cast<quint64>(f.size()) != expectedSize) {
        error = QStringLiteral("размер %1 (%2) не равен ожидаемому %3")
                    .arg(path)
                    .arg(f.size())
                    .arg(expectedSize);
        return false;
    }

    Crc32 crc;
    QByteArray buf;
    buf.resize(kCrcReadBlock);
    for (;;) {
        const qint64 n = f.read(buf.data(), buf.size());
        if (n < 0) {
            error = QStringLiteral("ошибка чтения %1: %2").arg(path, f.errorString());
            return false;
        }
        if (n == 0)
            break;
        crc.update(buf.constData(), n);
    }
    crcOut = crc.value();
    return true;
}
}  // namespace

Receiver::Receiver(QObject* parent) : QObject(parent)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::currentPath();
    setOutDir(dir);
}

Receiver::~Receiver()
{
    if (active_)
        saveMeta(true);
    closeFile();
}

void Receiver::setOutDir(const QString& dir)
{
    if (dir.isEmpty())
        return;
    outDir_ = dir;
    QDir().mkpath(outDir_);
}

void Receiver::setMtu(quint16 mtu)
{
    mtu_ = qBound<quint16>(23, mtu, 517);
}

void Receiver::setNotifying(bool on)
{
    notifying_ = on;
    warnedNotNotifying_ = false;
    emit logMessage(on ? QStringLiteral("Control notifications включены")
                       : QStringLiteral("Control notifications выключены"));
}

QString Receiver::sanitizeName(const QString& raw) const
{
    QString name = QFileInfo(raw).fileName();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("received.bin");

    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1F]")),
                 QStringLiteral("_"));
    if (name.size() > 200)
        name = name.left(200);
    return name;
}

QString Receiver::finalPathFor(const QString& name) const
{
    return QDir(outDir_).filePath(name);
}

QString Receiver::uniqueFinalPath(const QString& path) const
{
    if (!QFile::exists(path))
        return path;

    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix();
    for (int i = 1; i < 10000; ++i) {
        QString candidate = QDir(dir).filePath(
            base + QStringLiteral(" (%1)").arg(i)
            + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));
        if (!QFile::exists(candidate))
            return candidate;
    }
    return path + QStringLiteral(".received");
}

void Receiver::handleControl(const QByteArray& frame)
{
    proto::ControlMessage m;
    QString err;
    if (!proto::decodeControl(frame, m, &err)) {
        emit logMessage(QStringLiteral("битый control-кадр: %1").arg(err));
        return;
    }

    switch (m.type) {
    case proto::ControlType::Start:
        startTransfer(m);
        break;
    case proto::ControlType::Fin:
        finishTransfer(m);
        break;
    case proto::ControlType::Abort:
        if (active_ && m.transferId == transferId_) {
            saveMeta(true);
            closeFile();
            active_ = false;
            emit logMessage(QStringLiteral("получатель: передача прервана: %1")
                                .arg(m.reason.isEmpty() ? proto::statusName(m.status)
                                                        : m.reason));
            emit stateChanged(QStringLiteral("прервано"));
            emit finished(false, QStringLiteral("передача прервана отправителем"));
        }
        break;
    case proto::ControlType::Ack:
    case proto::ControlType::StartAck:
    case proto::ControlType::FinAck:
        // Это сообщения отправителя; на периферии они не обрабатываются.
        break;
    }
}

void Receiver::startTransfer(const proto::ControlMessage& msg)
{
    if (msg.transferId.size() != proto::kTransferIdSize) {
        emit logMessage(QStringLiteral("START с некорректным transferId"));
        return;
    }

    // Запоминаем состояние предыдущего transfer до перезаписи полей.
    const QByteArray prevId = transferId_;
    const quint64 prevSize = totalSize_;
    const quint64 prevExpected = expected_;

    // Уже идёт этот же transfer — просто подтверждаем текущий offset.
    if (active_ && msg.transferId == transferId_ && msg.totalSize == totalSize_) {
        proto::ControlMessage ack;
        ack.type = proto::ControlType::StartAck;
        ack.transferId = transferId_;
        ack.status = expected_ > 0 ? proto::Status::Resume : proto::Status::Ok;
        ack.offset = expected_;
        ack.mtu = mtu_;
        ack.chunkSize = chunkSize_;
        emit controlToSend(proto::encodeControl(ack));
        return;
    }

    // Другой активный transfer: аккуратно закрываем, оставляя .part для resume.
    if (active_) {
        saveMeta(true);
        closeFile();
        active_ = false;
        emit logMessage(QStringLiteral("начат новый transfer, предыдущий сохранён"));
    }

    const QString name = sanitizeName(msg.fileName);
    const QString finalPath = finalPathFor(name);
    const QString partPath = finalPath + QStringLiteral(".part");
    const QString sidecarPath = finalPath + QStringLiteral(".part.json");

    // Если итоговый файл уже есть и его CRC32 совпадает — передача не нужна.
    if (QFile::exists(finalPath) && static_cast<quint64>(QFileInfo(finalPath).size()) == msg.totalSize) {
        quint32 crc = 0;
        QString crcErr;
        if (crcOfFile(finalPath, msg.totalSize, crc, crcErr) && crc == msg.crc) {
            transferId_ = msg.transferId;
            totalSize_ = msg.totalSize;
            expected_ = msg.totalSize;
            fileName_ = name;
            finalPath_ = finalPath;
            partPath_ = partPath;
            sidecarPath_ = sidecarPath;
            chunkSize_ = msg.chunkSize;
            haveExpectedCrc_ = true;
            expectedCrc_ = msg.crc;
            active_ = false;
            rate_.reset(expected_);
            emit progress(expected_, totalSize_);
            emit stateChanged(QStringLiteral("файл уже принят"));
            emit finished(true, QStringLiteral("файл уже есть, CRC32 совпал"));
            proto::ControlMessage ack;
            ack.type = proto::ControlType::StartAck;
            ack.transferId = transferId_;
            ack.status = proto::Status::Complete;
            ack.offset = expected_;
            ack.mtu = mtu_;
            ack.chunkSize = chunkSize_;
            emit controlToSend(proto::encodeControl(ack));
            return;
        }
        emit logMessage(QStringLiteral("найден файл с тем же именем, но CRC32 не совпал — начинаем заново"));
    }

    fileName_ = name;
    finalPath_ = finalPath;
    partPath_ = partPath;
    sidecarPath_ = sidecarPath;
    transferId_ = msg.transferId;
    totalSize_ = msg.totalSize;
    chunkSize_ = msg.chunkSize > 0 ? msg.chunkSize : static_cast<quint16>(qMax(1, mtu_ - 3 - proto::kChunkHeaderSize));
    expectedCrc_ = msg.crc;
    haveExpectedCrc_ = true;

    quint64 resumeAt = 0;

    TransferMeta meta;
    const bool haveMeta = ResumeStore::load(sidecarPath_, meta);
    const bool metaMatches = haveMeta && meta.transferId == msg.transferId
                             && meta.totalSize == msg.totalSize;

    const bool memoryMatches = !prevId.isEmpty() && prevId == msg.transferId
                               && prevSize == msg.totalSize && prevExpected > 0;

    if ((metaMatches || memoryMatches) && QFile::exists(partPath_)) {
        if (!part_.isOpen()) {
            part_.setFileName(partPath_);
            if (!part_.open(QIODevice::ReadWrite)) {
                failReceive(QStringLiteral("не удалось открыть %1: %2")
                                .arg(partPath_, part_.errorString()));
                return;
            }
        }
        const quint64 actual = static_cast<quint64>(qMax<qint64>(0, part_.size()));
        resumeAt = metaMatches ? meta.received : prevExpected;
        resumeAt = std::min(resumeAt, actual);
        resumeAt = std::min(resumeAt, totalSize_);
        if (part_.size() != static_cast<qint64>(resumeAt)) {
            if (!part_.resize(static_cast<qint64>(resumeAt))) {
                failReceive(QStringLiteral("не удалось обрезать .part: %1").arg(part_.errorString()));
                return;
            }
        }
        if (!part_.seek(static_cast<qint64>(resumeAt))) {
            failReceive(QStringLiteral("не удалось перейти на offset %1").arg(resumeAt));
            return;
        }
        emit logMessage(QStringLiteral("возобновление %1 с offset=%2")
                            .arg(fileName_)
                            .arg(resumeAt));
    } else {
        if (QFile::exists(partPath_))
            QFile::remove(partPath_);
        part_.setFileName(partPath_);
        if (!part_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failReceive(QStringLiteral("не удалось создать %1: %2")
                            .arg(partPath_, part_.errorString()));
            return;
        }
        resumeAt = 0;
        emit logMessage(QStringLiteral("приём %1 (%2 байт), chunk=%3")
                            .arg(fileName_)
                            .arg(totalSize_)
                            .arg(chunkSize_));
    }

    expected_ = resumeAt;
    lastAck_ = expected_;
    lastSaved_ = expected_;
    rate_.reset(expected_);
    ackTimer_.restart();
    saveTimer_.restart();
    active_ = true;

    saveMeta(true);
    emit progress(expected_, totalSize_);
    emit stateChanged(QStringLiteral("приём"));

    proto::ControlMessage ack;
    ack.type = proto::ControlType::StartAck;
    ack.transferId = transferId_;
    ack.status = expected_ > 0 ? proto::Status::Resume : proto::Status::Ok;
    ack.offset = expected_;
    ack.mtu = mtu_;
    ack.chunkSize = chunkSize_;
    emit controlToSend(proto::encodeControl(ack));
}

bool Receiver::handleChunk(const QByteArray& frame, QString* error)
{
    proto::Chunk c;
    QString err;
    if (!proto::decodeChunk(frame, c, &err)) {
        if (error)
            *error = err;
        emit logMessage(QStringLiteral("чанк отвергнут: %1").arg(err));
        if (active_)
            sendAck(proto::Status::Error, true, expected_);
        return false;
    }

    if (!active_) {
        if (error)
            *error = QStringLiteral("нет активной передачи");
        emit logMessage(QStringLiteral("чанк без активной передачи — игнор"));
        return false;
    }

    if (c.offset > totalSize_ || c.offset + static_cast<quint64>(c.payload.size()) > totalSize_) {
        if (error)
            *error = QStringLiteral("чанк выходит за размер файла");
        emit logMessage(QStringLiteral("чанк за пределами файла (offset=%1, len=%2)")
                            .arg(c.offset)
                            .arg(c.payload.size()));
        sendAck(proto::Status::Error, true, expected_);
        return false;
    }

    if (c.offset < expected_) {
        // Дубликат: уже записано, просто подтверждаем текущий offset.
        if (expected_ - lastAck_ >= kAckInterval || ackTimer_.elapsed() >= kAckTimerMs)
            sendAck(proto::Status::Ok, false);
        return true;
    }

    if (c.offset > expected_) {
        if (error)
            *error = QStringLiteral("разрыв: ожидался offset %1, пришёл %2")
                         .arg(expected_)
                         .arg(c.offset);
        emit logMessage(QStringLiteral("разрыв: ожидался offset %1, пришёл %2")
                            .arg(expected_)
                            .arg(c.offset));
        sendAck(proto::Status::Error, true, expected_);
        return false;
    }

    const qint64 written = part_.write(c.payload);
    if (written != c.payload.size()) {
        const QString msg = QStringLiteral("ошибка записи .part: %1").arg(part_.errorString());
        if (error)
            *error = msg;
        failReceive(msg);
        return false;
    }

    expected_ += static_cast<quint64>(c.payload.size());
    rate_.update(expected_);
    emit progress(expected_, totalSize_);

    saveMeta(false);
    // Если до конца файла меньше ACK-интервала, подтверждаем сразу:
    // иначе отправитель может навсегда застрять в ожидании последнего ACK.
    const bool tail = expected_ >= totalSize_ || (totalSize_ - expected_) < kAckInterval;
    sendAck(proto::Status::Ok, tail);
    return true;
}

void Receiver::sendAck(proto::Status status, bool force, quint64 offset)
{
    if (!active_)
        return;
    if (!notifying_) {
        if (!warnedNotNotifying_) {
            emit logMessage(QStringLiteral(
                "ACK не отправлен: central не подписан на Control notifications"));
            warnedNotNotifying_ = true;
        }
        return;
    }

    const bool due = force || (expected_ - lastAck_) >= kAckInterval
                     || ackTimer_.elapsed() >= kAckTimerMs;
    if (!due)
        return;

    proto::ControlMessage ack;
    ack.type = proto::ControlType::Ack;
    ack.transferId = transferId_;
    ack.offset = offset;
    if (offset == 0 && (status == proto::Status::Ok || status == proto::Status::Error))
        ack.offset = expected_;
    ack.status = status;
    emit controlToSend(proto::encodeControl(ack));

    if (status == proto::Status::Ok) {
        lastAck_ = expected_;
        ackTimer_.restart();
    }
}

void Receiver::finishTransfer(const proto::ControlMessage& msg)
{
    if (!active_ || msg.transferId != transferId_) {
        emit logMessage(QStringLiteral("FIN без активной передачи"));
        proto::ControlMessage ack;
        ack.type = proto::ControlType::FinAck;
        ack.transferId = msg.transferId;
        ack.status = proto::Status::Error;
        ack.crc = 0;
        if (notifying_)
            emit controlToSend(proto::encodeControl(ack));
        return;
    }

    if (msg.totalSize != totalSize_) {
        proto::ControlMessage ack;
        ack.type = proto::ControlType::FinAck;
        ack.transferId = transferId_;
        ack.status = proto::Status::SizeMismatch;
        ack.crc = 0;
        emit controlToSend(proto::encodeControl(ack));
        failReceive(QStringLiteral("FIN: размер не совпал (%1 != %2)")
                        .arg(msg.totalSize)
                        .arg(totalSize_));
        return;
    }

    part_.flush();
    closeFile();

    quint32 actualCrc = 0;
    QString crcErr;
    if (!verifyFileCrc(actualCrc, crcErr)) {
        failReceive(crcErr);
        return;
    }

    const bool crcMatches = (actualCrc == msg.crc)
                            && (!haveExpectedCrc_ || actualCrc == expectedCrc_);
    if (!crcMatches) {
        active_ = false;
        // Повреждённый .part нельзя использовать для resume: удаляем,
        // чтобы повторная отправка началась с offset=0.
        QFile::remove(partPath_);
        ResumeStore::remove(sidecarPath_);
        expected_ = 0;
        lastAck_ = 0;
        lastSaved_ = 0;
        emit logMessage(QStringLiteral("CRC32 не совпал: получено %1, ожидалось %2; .part удалён")
                            .arg(actualCrc, 8, 16, QLatin1Char('0'))
                            .arg(msg.crc, 8, 16, QLatin1Char('0')));
        emit stateChanged(QStringLiteral("CRC32 ошибка"));
        emit finished(false, QStringLiteral("CRC32 не совпал"));
        proto::ControlMessage ack;
        ack.type = proto::ControlType::FinAck;
        ack.transferId = transferId_;
        ack.status = proto::Status::CrcMismatch;
        ack.crc = actualCrc;
        if (notifying_)
            emit controlToSend(proto::encodeControl(ack));
        return;
    }

    const QString unique = uniqueFinalPath(finalPath_);
    if (!QFile::rename(partPath_, unique)) {
        failReceive(QStringLiteral("не удалось переименовать %1 → %2")
                        .arg(partPath_, unique));
        return;
    }

    ResumeStore::remove(sidecarPath_);
    active_ = false;
    lastSaved_ = 0;
    emit logMessage(QStringLiteral("файл сохранён: %1 (CRC32=%2)")
                        .arg(unique)
                        .arg(actualCrc, 8, 16, QLatin1Char('0')));
    emit progress(totalSize_, totalSize_);
    emit stateChanged(QStringLiteral("готово"));
    emit finished(true, QStringLiteral("файл принят, CRC32 совпал"));

    proto::ControlMessage ack;
    ack.type = proto::ControlType::FinAck;
    ack.transferId = transferId_;
    ack.status = proto::Status::Ok;
    ack.crc = actualCrc;
    if (notifying_)
        emit controlToSend(proto::encodeControl(ack));
}

bool Receiver::verifyFileCrc(quint32& crcOut, QString& error)
{
    return crcOfFile(partPath_, totalSize_, crcOut, error);
}

void Receiver::onTransportLost()
{
    if (!active_)
        return;
    saveMeta(true);
    closeFile();
    active_ = false;
    emit logMessage(QStringLiteral("соединение потеряно, .part сохранён для resume"));
    emit stateChanged(QStringLiteral("ожидание переподключения"));
}

void Receiver::abort(const QString& reason)
{
    if (!active_)
        return;
    saveMeta(true);
    closeFile();
    active_ = false;
    emit stateChanged(QStringLiteral("отменено"));
    emit finished(false, reason.isEmpty() ? QStringLiteral("приём отменён") : reason);
}

void Receiver::closeFile()
{
    if (part_.isOpen()) {
        part_.flush();
        part_.close();
    }
}

void Receiver::saveMeta(bool force)
{
    if (!active_ && !force)
        return;
    if (partPath_.isEmpty())
        return;
    if (!force && expected_ - lastSaved_ < kMetaInterval && saveTimer_.elapsed() < kMetaTimerMs)
        return;

    TransferMeta meta;
    meta.transferId = transferId_;
    meta.fileName = fileName_;
    meta.totalSize = totalSize_;
    meta.chunkSize = chunkSize_;
    meta.received = expected_;
    meta.crc = expectedCrc_;
    meta.completed = false;
    ResumeStore::save(sidecarPath_, meta);

    lastSaved_ = expected_;
    saveTimer_.restart();
}

void Receiver::failReceive(const QString& message)
{
    if (active_) {
        saveMeta(true);
        closeFile();
    }
    active_ = false;
    emit logMessage(QStringLiteral("ошибка приёма: %1").arg(message));
    emit stateChanged(QStringLiteral("ошибка"));
    emit finished(false, message);
}
