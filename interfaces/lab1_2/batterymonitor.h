#pragma once

#include <QObject>
#include <QString>

class BatteryMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int chargeLevel READ chargeLevel NOTIFY changed)
    Q_PROPERTY(bool acOnline READ acOnline NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
    explicit BatteryMonitor(QObject *parent = nullptr);

    bool available() const { return m_available; }
    int chargeLevel() const { return m_chargeLevel; }
    bool acOnline() const { return m_acOnline; }
    QString statusText() const;

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    bool m_available = false;
    int m_chargeLevel = 0;
    bool m_acOnline = false;
};
