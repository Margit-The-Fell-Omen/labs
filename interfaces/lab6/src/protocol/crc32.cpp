#include "protocol/crc32.h"

#include <array>

namespace {
// Таблица генерируется один раз при первом обращении.
const std::array<quint32, 256>& table()
{
    static const std::array<quint32, 256> t = [] {
        std::array<quint32, 256> out{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            out[i] = c;
        }
        return out;
    }();
    return t;
}
}  // namespace

Crc32::Crc32() = default;

void Crc32::reset()
{
    crc_ = 0xFFFFFFFFu;
}

void Crc32::update(const void* data, qsizetype len)
{
    if (!data || len <= 0)
        return;
    const auto* p = static_cast<const quint8*>(data);
    const auto& t = table();
    quint32 c = crc_;
    for (qsizetype i = 0; i < len; ++i)
        c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    crc_ = c;
}

void Crc32::update(const QByteArray& data)
{
    update(data.constData(), data.size());
}

quint32 Crc32::value() const
{
    return crc_ ^ 0xFFFFFFFFu;
}

quint32 Crc32::compute(const QByteArray& data)
{
    Crc32 c;
    c.update(data);
    return c.value();
}
