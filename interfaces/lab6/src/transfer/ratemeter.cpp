#include "transfer/ratemeter.h"

RateMeter::RateMeter()
{
    reset();
}

void RateMeter::reset(quint64 bytes)
{
    timer_.start();
    lastBytes_ = bytes;
    lastMs_ = 0;
    emaBytesPerSec_ = 0.0;
}

void RateMeter::update(quint64 bytes)
{
    if (!timer_.isValid())
        timer_.start();

    const qint64 now = timer_.elapsed();
    const qint64 dt = now - lastMs_;
    if (dt <= 0)
        return;

    if (bytes < lastBytes_)
        lastBytes_ = bytes;

    const double instant = static_cast<double>(bytes - lastBytes_) * 1000.0 / dt;
    emaBytesPerSec_ = (emaBytesPerSec_ <= 0.0)
                          ? instant
                          : (0.7 * emaBytesPerSec_ + 0.3 * instant);

    lastBytes_ = bytes;
    lastMs_ = now;
}

qint64 RateMeter::etaSeconds(quint64 total, quint64 done) const
{
    if (done >= total)
        return 0;
    if (emaBytesPerSec_ < 1.0)
        return -1;
    const quint64 left = total - done;
    return static_cast<qint64>(left / emaBytesPerSec_);
}
