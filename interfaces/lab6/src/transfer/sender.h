#pragma once

// Отправитель: читает файл, режет на чанки, соблюдает окно неподтверждённых
// байт и возобновляет передачу после переподключения.

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include "protocol/frame.h"
#include "transfer/ratemeter.h"

class Sender : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        WaitingReady,
        WaitingStartAck,
        Sending,
        Finishing,
        Done,
        Error,
    };

    explicit Sender(QObject* parent = nullptr);
    ~Sender() override;

    bool start(const QString& filePath, QString* error = nullptr);
    void abort(const QString& reason = {});
    void onTransportReady(quint16 mtu);
    void onTransportLost();
    void onControl(const QByteArray& frame);

    QByteArray takeChunk();
    void pump();
    bool canSendMore() const;

    bool isActive() const;
    bool isFinished() const;
    bool checkStall();

    void setWindow(quint64 bytes);
    void setChunkOverride(quint16 bytes);
    void setCorruptChunk(int index) { corruptChunk_ = index; }

    State state() const { return state_; }
    QString stateText() const;
    quint64 ackedOffset() const { return acked_; }
    quint64 totalSize() const { return totalSize_; }
    quint16 chunkSize() const { return chunk_; }
    QByteArray transferId() const { return transferId_; }
    QString fileName() const { return info_.fileName(); }
    const RateMeter& rateMeter() const { return rate_; }

signals:
    void controlToSend(const QByteArray& message);
    void progress(quint64 acked, quint64 total);
    void stateChanged(const QString& text);
    void finished(bool ok, const QString& message);
    void logMessage(const QString& text);

private:
    void fail(const QString& message);
    void sendStart();
    void sendFin();
    void updateChunkSize(quint16 mtu);
    void noteActivity();
    quint32 computeFileCrc();

    QFile file_;
    QFileInfo info_;
    QByteArray transferId_;
    quint64 totalSize_ = 0;
    quint32 fileCrc_ = 0;
    quint64 acked_ = 0;
    quint64 next_ = 0;
    quint16 chunk_ = 20;
    quint16 chunkOverride_ = 0;
    quint64 window_ = 64 * 1024;
    State state_ = State::Idle;
    RateMeter rate_;
    QElapsedTimer activity_;
    int corruptChunk_ = -1;
    int chunkIndex_ = 0;
};
