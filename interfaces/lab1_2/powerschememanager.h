#pragma once

#include <QObject>
#include <QStringList>
#include <vector>

#include <windows.h>

class PowerSchemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList schemes READ schemes NOTIFY changed)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY changed)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY changed)
    Q_PROPERTY(int displayTimeoutAC READ displayTimeoutAC NOTIFY changed)
    Q_PROPERTY(int displayTimeoutDC READ displayTimeoutDC NOTIFY changed)
    Q_PROPERTY(int sleepTimeoutAC READ sleepTimeoutAC NOTIFY changed)
    Q_PROPERTY(int sleepTimeoutDC READ sleepTimeoutDC NOTIFY changed)
    Q_PROPERTY(int criticalBatteryAC READ criticalBatteryAC NOTIFY changed)
    Q_PROPERTY(int criticalBatteryDC READ criticalBatteryDC NOTIFY changed)

public:
    explicit PowerSchemeManager(QObject *parent = nullptr);

    QStringList schemes() const;
    int activeIndex() const { return m_activeIndex; }
    int selectedIndex() const { return m_selectedIndex; }
    QString selectedName() const;
    int displayTimeoutAC() const { return m_displayAC; }
    int displayTimeoutDC() const { return m_displayDC; }
    int sleepTimeoutAC() const { return m_sleepAC; }
    int sleepTimeoutDC() const { return m_sleepDC; }
    int criticalBatteryAC() const { return m_criticalAC; }
    int criticalBatteryDC() const { return m_criticalDC; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void activate(int index);

signals:
    void changed();

private:
    void loadParameters(int index);

    struct Scheme {
        GUID guid;
        QString name;
    };

    std::vector<Scheme> m_schemes;
    int m_activeIndex = -1;
    int m_selectedIndex = -1;
    int m_displayAC = -1;
    int m_displayDC = -1;
    int m_sleepAC = -1;
    int m_sleepDC = -1;
    int m_criticalAC = -1;
    int m_criticalDC = -1;
};
