#include "hexdumpview.h"

#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>

HexDumpView::HexDumpView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void HexDumpView::setData(const QByteArray &data)
{
    m_data = data;
    m_hovered = -1;
    update();
}

void HexDumpView::setChangedOffsets(const QSet<int> &offsets)
{
    m_changed = offsets;
    update();
}

QSize HexDumpView::sizeHint() const
{
    return QSize(640, 160);
}

QSize HexDumpView::minimumSizeHint() const
{
    return QSize(480, 120);
}

void HexDumpView::layoutCells()
{
    const QFontMetrics fm(font());
    m_charWidth = fm.horizontalAdvance(QLatin1Char('0'));
    m_rowHeight = fm.height() + 4;
    const int pad = 8;

    const int fits = qMax(16, (width() - pad) / qMax(1, m_charWidth));
    m_bytesPerRow = qBound(4, (fits - 9) / 4, 32);

    m_offsetX = pad;
    m_hexStartX = m_offsetX + m_charWidth * 4 + m_charWidth * 2;
    m_asciiX = m_hexStartX + m_charWidth * (m_bytesPerRow * 3 + 1) + m_charWidth * 2;

    m_byteRects.clear();
    const int rows = (m_data.size() + m_bytesPerRow - 1) / m_bytesPerRow;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < m_bytesPerRow; ++c) {
            const int index = r * m_bytesPerRow + c;
            if (index >= m_data.size())
                continue;
            int x = m_hexStartX + c * m_charWidth * 3;
            if (c >= 8)
                x += m_charWidth;
            m_byteRects.append(QRect(x, r * m_rowHeight + 2, m_charWidth * 2, m_rowHeight - 2));
        }
    }
}

void HexDumpView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void HexDumpView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    layoutCells();

    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    if (!m_data.isEmpty())
        painter.fillRect(rect(), QColor(255, 255, 255));

    painter.setFont(font());
    const int rows = (m_data.size() + m_bytesPerRow - 1) / m_bytesPerRow;

    for (int r = 0; r < rows; ++r) {
        const int y = r * m_rowHeight;
        const int rowStart = r * m_bytesPerRow;

        painter.setPen(palette().color(QPalette::Text));
        const QString offset = QStringLiteral("%1")
            .arg(rowStart, 4, 16, QLatin1Char('0'));
        painter.drawText(m_offsetX, y, m_charWidth * 4, m_rowHeight,
                         Qt::AlignVCenter | Qt::AlignLeft, offset);

        for (int c = 0; c < m_bytesPerRow; ++c) {
            const int index = rowStart + c;
            if (index >= m_data.size())
                break;
            const int x = m_hexStartX + c * m_charWidth * 3 + (c >= 8 ? m_charWidth : 0);
            const QRect cell(x, y + 2, m_charWidth * 2, m_rowHeight - 2);

            if (index == m_hovered || m_changed.contains(index)) {
                const QColor bg = (index == m_hovered) ? QColor(200, 225, 255)
                                                       : QColor(255, 245, 180);
                painter.fillRect(cell, bg);
            }

            const uchar byte = static_cast<uchar>(m_data[index]);
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(cell, Qt::AlignVCenter | Qt::AlignLeft,
                             QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')));
        }

        for (int c = 0; c < m_bytesPerRow; ++c) {
            const int index = rowStart + c;
            if (index >= m_data.size())
                break;
            const uchar byte = static_cast<uchar>(m_data[index]);
            const QChar ch = (byte >= 0x20 && byte < 0x7F) ? QChar(byte) : QLatin1Char('.');
            const int x = m_asciiX + c * m_charWidth;
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(x, y, m_charWidth, m_rowHeight,
                             Qt::AlignVCenter | Qt::AlignLeft, QString(ch));
        }
    }
}

void HexDumpView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->pos();
    int found = -1;
    for (int i = 0; i < m_byteRects.size(); ++i) {
        if (m_byteRects[i].contains(pos)) {
            found = i;
            break;
        }
    }
    if (found != m_hovered) {
        m_hovered = found;
        update();
    }
    emit byteHovered(found);
}

void HexDumpView::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = -1;
    update();
    emit byteHovered(-1);
}