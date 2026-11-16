#pragma once

// Приёмник: складывает чанки в <file>.part, ведёт <file>.part.json,
// подтверждает offset через ACK и в конце проверяет CRC32 всего файла.

#include <QElapsedTimer>
#include <QFile>
#include <QObject>

#include "protocol/frame.h"
#include "transfer/ratemeter.h"

class Receiver : public QObject {
    Q_OBJECT
public:
    explicit Receiver(QObject* parent = nullptr);
    ~Receiver() override;

    void setOutDir(const QString& dir);
    QString outDir() const { return outDir_; }

    void setMtu(quint16 mtu);
    quint16 mtu() const { return mtu_; }
    void setNotifying(bool on);

    void handleControl(const QByteArray& frame);
    bool handleChunk(const QByteArray& frame, QString* error = nullptr);
    void onTransportLost();
    void abort(const QString& reason = {});

    bool isActive() const { return active_; }
    quint64 received() const { return expected_; }
    quint64 totalSize() const { return totalSize_; }
    QString currentFileName() const { return fileName_; }
    const RateMeter& rateMeter() const { return rate_; }

signals:
    void controlToSend(const QByteArray& message);
    void progress(quint64 received, quint64 total);
    void stateChanged(const QString& text);
    void finished(bool ok, const QString& message);
    void logMessage(const QString& text);

private:
    void startTransfer(const proto::ControlMessage& msg);
    void finishTransfer(const proto::ControlMessage& msg);
    void sendAck(proto::Status status, bool force, quint64 offset = 0);
    void closeFile();
    void saveMeta(bool force);
    void failReceive(const QString& message);
    bool verifyFileCrc(quint32& crcOut, QString& error);
    QString finalPathFor(const QString& name) const;
    QString uniqueFinalPath(const QString& path) const;
    QString sanitizeName(const QString& raw) const;

    QString outDir_;
    QFile part_;
    QString partPath_;
    QString finalPath_;
    QString sidecarPath_;
    QString fileName_;

    QByteArray transferId_;
    quint64 totalSize_ = 0;
    quint64 expected_ = 0;
    quint64 lastAck_ = 0;
    quint64 lastSaved_ = 0;
    quint32 expectedCrc_ = 0;
    bool haveExpectedCrc_ = false;
    quint16 mtu_ = 23;
    quint16 chunkSize_ = 20;
    bool active_ = false;
    bool notifying_ = false;
    bool warnedNotNotifying_ = false;

    RateMeter rate_;
    QElapsedTimer ackTimer_;
    QElapsedTimer saveTimer_;
};
