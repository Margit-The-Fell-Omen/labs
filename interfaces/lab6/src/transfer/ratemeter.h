#pragma once

// Оценка скорости и оставшегося времени (скользящее среднее).

#include <QElapsedTimer>
#include <QtGlobal>

class RateMeter {
public:
    RateMeter();

    void reset(quint64 bytes = 0);
    void update(quint64 bytes);

    double bytesPerSecond() const { return emaBytesPerSec_; }
    double kibPerSecond() const { return emaBytesPerSec_ / 1024.0; }
    qint64 etaSeconds(quint64 total, quint64 done) const;

private:
    QElapsedTimer timer_;
    quint64 lastBytes_ = 0;
    qint64 lastMs_ = 0;
    double emaBytesPerSec_ = 0.0;
};
