#include <QtTest/QtTest>

#include "protocol/crc32.h"

class TestCrc32 : public QObject {
    Q_OBJECT
private slots:
    void knownVector()
    {
        QCOMPARE(Crc32::compute(QByteArrayLiteral("123456789")), 0xCBF43926u);
    }

    void empty()
    {
        QCOMPARE(Crc32::compute(QByteArray()), 0u);
    }

    void incrementalEqualsOneShot()
    {
        QByteArray data;
        for (int i = 0; i < 10000; ++i)
            data.append(static_cast<char>((i * 37) & 0xFF));

        Crc32 c;
        for (int off = 0; off < data.size(); off += 137)
            c.update(data.constData() + off, qMin(137, data.size() - off));

        QCOMPARE(c.value(), Crc32::compute(data));
    }

    void resetWorks()
    {
        Crc32 c;
        c.update(QByteArrayLiteral("junk"));
        c.reset();
        c.update(QByteArrayLiteral("123456789"));
        QCOMPARE(c.value(), 0xCBF43926u);
    }
};

QTEST_MAIN(TestCrc32)
#include "test_crc32.moc"
