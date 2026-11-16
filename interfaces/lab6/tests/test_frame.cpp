#include <QtTest/QtTest>

#include "protocol/crc32.h"
#include "protocol/frame.h"

using namespace proto;

class TestFrame : public QObject {
    Q_OBJECT
private slots:
    void chunkRoundTrip()
    {
        const QByteArray payload = QByteArrayLiteral("hello BLE file transfer");
        const QByteArray frame = encodeChunk(0x1122334455667788ull, payload);
        QCOMPARE(frame.size(), kChunkHeaderSize + payload.size());

        Chunk c;
        QString err;
        QVERIFY2(decodeChunk(frame, c, &err), qPrintable(err));
        QCOMPARE(c.offset, 0x1122334455667788ull);
        QCOMPARE(c.payload, payload);
        QCOMPARE(c.crc, Crc32::compute(payload));
    }

    void chunkCorruptionDetected()
    {
        QByteArray frame = encodeChunk(10, QByteArrayLiteral("abcdef"));
        frame[frame.size() - 1] = static_cast<char>(frame.back() ^ 0x01);
        Chunk c;
        QString err;
        QVERIFY(!decodeChunk(frame, c, &err));
        QVERIFY(!err.isEmpty());
    }

    void chunkTooShort()
    {
        Chunk c;
        QVERIFY(!decodeChunk(QByteArray(5, '\0'), c));
    }

    void startRoundTrip()
    {
        ControlMessage m;
        m.type = ControlType::Start;
        m.transferId = QByteArray::fromHex("00112233445566778899aabbccddeeff");
        m.totalSize = 123456789;
        m.chunkSize = 244;
        m.crc = 0xDEADBEEF;
        m.fileName = QStringLiteral("тестовый файл.bin");

        const QByteArray frame = encodeControl(m);
        ControlMessage out;
        QString err;
        QVERIFY2(decodeControl(frame, out, &err), qPrintable(err));
        QCOMPARE(out.type, ControlType::Start);
        QCOMPARE(out.transferId, m.transferId);
        QCOMPARE(out.totalSize, m.totalSize);
        QCOMPARE(out.chunkSize, m.chunkSize);
        QCOMPARE(out.crc, m.crc);
        QCOMPARE(out.fileName, m.fileName);
    }

    void ackRoundTrip()
    {
        ControlMessage m;
        m.type = ControlType::Ack;
        m.transferId = QByteArray(16, 'x');
        m.offset = 0x0102030405060708ull;
        m.status = Status::Error;

        ControlMessage out;
        QVERIFY(decodeControl(encodeControl(m), out));
        QCOMPARE(out.type, ControlType::Ack);
        QCOMPARE(out.offset, m.offset);
        QCOMPARE(out.status, Status::Error);
    }

    void startAckRoundTrip()
    {
        ControlMessage m;
        m.type = ControlType::StartAck;
        m.transferId = QByteArray(16, 'y');
        m.status = Status::Resume;
        m.offset = 4096;
        m.mtu = 247;
        m.chunkSize = 232;

        ControlMessage out;
        QVERIFY(decodeControl(encodeControl(m), out));
        QCOMPARE(out.type, ControlType::StartAck);
        QCOMPARE(out.status, Status::Resume);
        QCOMPARE(out.offset, 4096ull);
        QCOMPARE(out.mtu, 247);
        QCOMPARE(out.chunkSize, 232);
    }

    void finRoundTrip()
    {
        ControlMessage m;
        m.type = ControlType::Fin;
        m.transferId = QByteArray(16, 'z');
        m.totalSize = 99;
        m.crc = 0x12345678;

        ControlMessage out;
        QVERIFY(decodeControl(encodeControl(m), out));
        QCOMPARE(out.type, ControlType::Fin);
        QCOMPARE(out.totalSize, 99ull);
        QCOMPARE(out.crc, 0x12345678u);
    }

    void truncatedControlRejected()
    {
        ControlMessage m;
        m.type = ControlType::Start;
        m.transferId = QByteArray(16, 'a');
        m.totalSize = 10;
        m.chunkSize = 8;
        m.crc = 1;
        m.fileName = QStringLiteral("x");
        QByteArray frame = encodeControl(m);
        frame.chop(2);

        ControlMessage out;
        QVERIFY(!decodeControl(frame, out));
    }

    void transferIdIsStable()
    {
        const QByteArray a = makeTransferId(QStringLiteral("/tmp/a"), 100, 123456);
        const QByteArray b = makeTransferId(QStringLiteral("/tmp/a"), 100, 123456);
        const QByteArray c = makeTransferId(QStringLiteral("/tmp/a"), 101, 123456);
        QCOMPARE(a.size(), kTransferIdSize);
        QCOMPARE(a, b);
        QVERIFY(a != c);
    }
};

QTEST_MAIN(TestFrame)
#include "test_frame.moc"
