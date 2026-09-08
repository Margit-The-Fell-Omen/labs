#pragma once

#include <QObject>

class BatteryInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString chemistry READ chemistry NOTIFY changed)
    Q_PROPERTY(int designCapacity READ designCapacity NOTIFY changed)
    Q_PROPERTY(int fullChargeCapacity READ fullChargeCapacity NOTIFY changed)
    Q_PROPERTY(int currentCapacity READ currentCapacity NOTIFY changed)
    Q_PROPERTY(int voltage READ voltage NOTIFY changed)
    Q_PROPERTY(int cycleCount READ cycleCount NOTIFY changed)
    Q_PROPERTY(int temperature READ temperature NOTIFY changed)
    Q_PROPERTY(qreal wearPercent READ wearPercent NOTIFY changed)
    Q_PROPERTY(qreal chargeLevel READ chargeLevel NOTIFY changed)
    Q_PROPERTY(bool acOnline READ acOnline NOTIFY changed)

public:
    explicit BatteryInfo(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString chemistry() const { return m_chemistry; }
    int designCapacity() const { return m_designCapacity; }
    int fullChargeCapacity() const { return m_fullChargeCapacity; }
    int currentCapacity() const { return m_currentCapacity; }
    int voltage() const { return m_voltage; }
    int cycleCount() const { return m_cycleCount; }
    int temperature() const { return m_temperature; }
    qreal wearPercent() const { return m_wearPercent; }
    qreal chargeLevel() const { return m_chargeLevel; }
    bool acOnline() const { return m_acOnline; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    bool m_available = false;
    QString m_chemistry;
    int m_designCapacity = 0;
    int m_fullChargeCapacity = 0;
    int m_currentCapacity = 0;
    int m_voltage = 0;
    int m_cycleCount = 0;
    int m_temperature = 0;
    qreal m_wearPercent = 0.0;
    qreal m_chargeLevel = 0.0;
    bool m_acOnline = false;
};
