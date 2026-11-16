#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "protocol/crc32.h"
#include "protocol/frame.h"
#include "transfer/receiver.h"
#include "transfer/sender.h"

namespace {

QByteArray randomData(int size)
{
    QByteArray data(size, Qt::Uninitialized);
    quint32 state = 0x12345678u;
    for (int i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        data[i] = static_cast<char>(state >> 24);
    }
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QString firstFinalFile(const QString& dir)
{
    const QStringList files = QDir(dir).entryList(QDir::Files, QDir::Name);
    for (const QString& name : files) {
        if (name.endsWith(QStringLiteral(".part")) || name.endsWith(QStringLiteral(".part.json")))
            continue;
        return QDir(dir).filePath(name);
    }
    return {};
}

bool pump(Sender& sender, Receiver& receiver, int maxIterations = 20000)
{
    int i = 0;
    while (!sender.isFinished()) {
        bool progressed = false;
        while (sender.canSendMore()) {
            const QByteArray frame = sender.takeChunk();
            if (frame.isEmpty())
                break;
            receiver.handleChunk(frame);
            progressed = true;
        }
        sender.pump();
        if (sender.isFinished())
            break;
        if (!progressed)
            QTest::qWait(2);
        if (++i > maxIterations)
            return false;
    }
    return sender.state() == Sender::State::Done;
}

}  // namespace

class TestTransfer : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QVERIFY(dir_.isValid());
        QDir(dir_.path()).removeRecursively();
        QVERIFY(QDir().mkpath(dir_.path()));
        srcDir_ = dir_.filePath(QStringLiteral("src"));
        outDir_ = dir_.filePath(QStringLiteral("out"));
        QVERIFY(QDir().mkpath(srcDir_));
        QVERIFY(QDir().mkpath(outDir_));
    }

    void cleanup() {}

    void fullTransfer()
    {
        const QByteArray data = randomData(300 * 1024);
        const QString src = QDir(srcDir_).filePath(QStringLiteral("source.bin"));
        QVERIFY(writeFile(src, data));

        Receiver rx;
        rx.setOutDir(outDir_);
        rx.setMtu(247);
        rx.setNotifying(true);

        Sender tx;
        tx.setWindow(64 * 1024);
        QObject::connect(&tx, &Sender::controlToSend, &rx, &Receiver::handleControl);
        QObject::connect(&rx, &Receiver::controlToSend, &tx, &Sender::onControl);

        QSignalSpy txDone(&tx, &Sender::finished);
        QSignalSpy rxDone(&rx, &Receiver::finished);

        QString err;
        QVERIFY2(tx.start(src, &err), qPrintable(err));
        tx.onTransportReady(247);
        QVERIFY(pump(tx, rx));

        QCOMPARE(txDone.count(), 1);
        QCOMPARE(rxDone.count(), 1);
        QVERIFY(txDone.first().at(0).toBool());
        QVERIFY(rxDone.first().at(0).toBool());

        const QString out = firstFinalFile(outDir_);
        QVERIFY2(!out.isEmpty(), "итоговый файл не найден");
        QCOMPARE(readFile(out), data);
        QCOMPARE(Crc32::compute(readFile(out)), Crc32::compute(data));
    }

    void disconnectAndResume()
    {
        const QByteArray data = randomData(512 * 1024);
        const QString src = QDir(srcDir_).filePath(QStringLiteral("resume.bin"));
        QVERIFY(writeFile(src, data));

        Receiver rx;
        rx.setOutDir(outDir_);
        rx.setMtu(247);
        rx.setNotifying(true);

        Sender tx;
        tx.setWindow(32 * 1024);
        QObject::connect(&tx, &Sender::controlToSend, &rx, &Receiver::handleControl);
        QObject::connect(&rx, &Receiver::controlToSend, &tx, &Sender::onControl);

        QString err;
        QVERIFY2(tx.start(src, &err), qPrintable(err));
        tx.onTransportReady(247);

        int delivered = 0;
        while (!tx.isFinished() && delivered < 400) {
            const QByteArray frame = tx.takeChunk();
            if (frame.isEmpty())
                break;
            rx.handleChunk(frame);
            ++delivered;
            tx.pump();
        }

        QVERIFY2(!tx.isFinished(),
                 qPrintable(QStringLiteral("state=%1 delivered=%2 acked=%3 total=%4")
                                .arg(tx.stateText())
                                .arg(delivered)
                                .arg(tx.ackedOffset())
                                .arg(tx.totalSize())));
        QVERIFY(rx.received() > 0);
        const quint64 before = rx.received();
        tx.onTransportLost();
        rx.onTransportLost();
        QCOMPARE(tx.state(), Sender::State::WaitingStartAck);

        tx.onTransportReady(247);
        QVERIFY(pump(tx, rx));

        const QString out = firstFinalFile(outDir_);
        QVERIFY2(!out.isEmpty(), "итоговый файл не найден");
        QCOMPARE(readFile(out), data);
        QVERIFY(rx.received() >= before);
    }

    void corruptedChunkIsRetransmitted()
    {
        const QByteArray data = randomData(200 * 1024);
        const QString src = QDir(srcDir_).filePath(QStringLiteral("corrupt.bin"));
        QVERIFY(writeFile(src, data));

        Receiver rx;
        rx.setOutDir(outDir_);
        rx.setMtu(247);
        rx.setNotifying(true);

        Sender tx;
        tx.setWindow(32 * 1024);
        tx.setCorruptChunk(10);
        QObject::connect(&tx, &Sender::controlToSend, &rx, &Receiver::handleControl);
        QObject::connect(&rx, &Receiver::controlToSend, &tx, &Sender::onControl);

        QString err;
        QVERIFY2(tx.start(src, &err), qPrintable(err));
        tx.onTransportReady(247);
        QVERIFY(pump(tx, rx));

        const QString out = firstFinalFile(outDir_);
        QVERIFY2(!out.isEmpty(), "итоговый файл не найден");
        QCOMPARE(readFile(out), data);
    }

    void finalCrcMismatchIsRejected()
    {
        const QByteArray data = randomData(64 * 1024);
        const QByteArray id = proto::makeTransferId(QStringLiteral("/tmp/badcrc"),
                                                    data.size(), 1234);

        Receiver rx;
        rx.setOutDir(outDir_);
        rx.setMtu(247);
        rx.setNotifying(true);
        QSignalSpy rxDone(&rx, &Receiver::finished);

        proto::ControlMessage start;
        start.type = proto::ControlType::Start;
        start.transferId = id;
        start.totalSize = data.size();
        start.chunkSize = 200;
        start.crc = Crc32::compute(data);
        start.fileName = QStringLiteral("badcrc.bin");
        rx.handleControl(proto::encodeControl(start));

        for (int offset = 0; offset < data.size(); offset += 200) {
            const QByteArray payload = data.mid(offset, 200);
            QVERIFY(rx.handleChunk(proto::encodeChunk(offset, payload)));
        }

        proto::ControlMessage fin;
        fin.type = proto::ControlType::Fin;
        fin.transferId = id;
        fin.totalSize = data.size();
        fin.crc = 0xDEADBEEFu;  // заведомо неверный
        rx.handleControl(proto::encodeControl(fin));

        QCOMPARE(rxDone.count(), 1);
        QVERIFY(!rxDone.first().at(0).toBool());
    }

    void emptyFile()
    {
        const QString src = QDir(srcDir_).filePath(QStringLiteral("empty.bin"));
        QVERIFY(writeFile(src, QByteArray()));

        Receiver rx;
        rx.setOutDir(outDir_);
        rx.setMtu(247);
        rx.setNotifying(true);

        Sender tx;
        QObject::connect(&tx, &Sender::controlToSend, &rx, &Receiver::handleControl);
        QObject::connect(&rx, &Receiver::controlToSend, &tx, &Sender::onControl);

        QString err;
        QVERIFY2(tx.start(src, &err), qPrintable(err));
        tx.onTransportReady(247);
        QVERIFY(pump(tx, rx));

        const QString out = firstFinalFile(outDir_);
        QVERIFY(!out.isEmpty());
        QCOMPARE(QFileInfo(out).size(), qint64(0));
    }

private:
    QTemporaryDir dir_;
    QString srcDir_;
    QString outDir_;
};

QTEST_MAIN(TestTransfer)
#include "test_transfer.moc"
