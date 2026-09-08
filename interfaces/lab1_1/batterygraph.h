#pragma once

#include <QQuickPaintedItem>

#include <vector>

class BatteryGraph : public QQuickPaintedItem
{
    Q_OBJECT
public:
    explicit BatteryGraph(QQuickItem *parent = nullptr);

    Q_INVOKABLE void addSample(qreal level);
    Q_INVOKABLE void clear();

    void paint(QPainter *painter) override;

private:
    std::vector<qreal> m_samples;
};
