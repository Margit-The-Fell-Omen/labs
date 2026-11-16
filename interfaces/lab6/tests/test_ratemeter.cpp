#include <QtTest/QtTest>

#include "transfer/ratemeter.h"

class TestRateMeter : public QObject {
    Q_OBJECT
private slots:
    void etaWhenDone()
    {
        RateMeter r;
        QCOMPARE(r.etaSeconds(100, 100), qint64(0));
    }

    void etaUnknownWithoutSpeed()
    {
        RateMeter r;
        QCOMPARE(r.etaSeconds(100, 50), qint64(-1));
    }

    void updateProducesPositiveSpeed()
    {
        RateMeter r;
        r.reset(0);
        QTest::qWait(25);
        r.update(1024);
        QVERIFY(r.bytesPerSecond() > 0.0);
        QVERIFY(r.etaSeconds(4096, 1024) >= 0);
    }
};

QTEST_MAIN(TestRateMeter)
#include "test_ratemeter.moc"
