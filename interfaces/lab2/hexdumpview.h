#pragma once

#include <QByteArray>
#include <QSet>
#include <QVector>
#include <QWidget>

class QRect;

class HexDumpView : public QWidget
{
    Q_OBJECT
public:
    explicit HexDumpView(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    void setChangedOffsets(const QSet<int> &offsets);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void byteHovered(int offset);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutCells();

    QByteArray m_data;
    QSet<int> m_changed;
    int m_hovered = -1;
    int m_bytesPerRow = 16;
    int m_rowHeight = 0;
    int m_charWidth = 0;
    int m_offsetX = 0;
    int m_hexStartX = 0;
    int m_asciiX = 0;
    QVector<QRect> m_byteRects;
};