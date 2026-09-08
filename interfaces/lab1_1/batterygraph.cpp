#include "batterygraph.h"

#include <QImage>
#include <QPainter>

#include <windows.h>
#include <gdiplus.h>

#include <string>

BatteryGraph::BatteryGraph(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
}

void BatteryGraph::addSample(qreal level)
{
    m_samples.push_back(level);
    if (m_samples.size() > 600)
        m_samples.erase(m_samples.begin());
    update();
}

void BatteryGraph::clear()
{
    m_samples.clear();
    update();
}

void BatteryGraph::paint(QPainter *painter)
{
    const int w = qMax(1, static_cast<int>(width()));
    const int h = qMax(1, static_cast<int>(height()));

    Gdiplus::Bitmap bitmap(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bitmap);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(255, 60, 56, 54));

        const int left = 46;
        const int top = 16;
        const int right = 14;
        const int bottom = 28;
        const int plotW = w - left - right;
        const int plotH = h - top - bottom;

        Gdiplus::Pen gridPen(Gdiplus::Color(255, 102, 92, 84), 1.0f);
        Gdiplus::Pen axisPen(Gdiplus::Color(255, 124, 111, 100), 1.2f);
        Gdiplus::SolidBrush labelBrush(Gdiplus::Color(255, 189, 174, 147));
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font font(&family, 9.0f);

        auto yFor = [&](int pct) {
            return static_cast<float>(top + plotH - (plotH * pct / 100));
        };

        for (int pct = 0; pct <= 100; pct += 25) {
            const float y = yFor(pct);
            g.DrawLine(&gridPen, static_cast<float>(left), y,
                       static_cast<float>(left + plotW), y);
            const std::wstring label = std::to_wstring(pct) + L"%";
            g.DrawString(label.c_str(), -1, &font, Gdiplus::PointF(2.0f, y - 7.0f),
                         &labelBrush);
        }

        const int ticks = 10;
        for (int i = 0; i <= ticks; ++i) {
            const float x = static_cast<float>(left + plotW * i / ticks);
            g.DrawLine(&gridPen, x, static_cast<float>(top), x,
                       static_cast<float>(top + plotH));
        }

        if (m_samples.size() >= 2) {
            const size_t n = m_samples.size();
            std::vector<Gdiplus::PointF> pts(n);
            for (size_t i = 0; i < n; ++i) {
                const float x = static_cast<float>(left + plotW * i / (n - 1));
                const qreal v = qBound<qreal>(0.0, m_samples[i], 100.0);
                const float y = yFor(static_cast<int>(v));
                pts[i] = Gdiplus::PointF(x, y);
            }

            std::vector<Gdiplus::PointF> area = pts;
            area.push_back(Gdiplus::PointF(pts.back().X, static_cast<float>(top + plotH)));
            area.push_back(Gdiplus::PointF(pts.front().X, static_cast<float>(top + plotH)));
            Gdiplus::SolidBrush fillBrush(Gdiplus::Color(40, 184, 187, 38));
            g.FillPolygon(&fillBrush, area.data(), static_cast<INT>(area.size()));

            Gdiplus::Pen linePen(Gdiplus::Color(255, 184, 187, 38), 2.0f);
            g.DrawLines(&linePen, pts.data(), static_cast<INT>(pts.size()));
        } else {
            Gdiplus::SolidBrush emptyBrush(Gdiplus::Color(255, 168, 153, 132));
            Gdiplus::FontFamily f2(L"Segoe UI");
            Gdiplus::Font fnt(&f2, 11.0f);
            g.DrawString(L"No data", -1, &fnt,
                         Gdiplus::PointF(static_cast<float>(left + 12),
                                         static_cast<float>(top + plotH / 2 - 8)),
                         &emptyBrush);
        }

        g.DrawRectangle(&axisPen, left, top, plotW, plotH);
    }

    Gdiplus::BitmapData data = {};
    const Gdiplus::Rect rect(0, 0, w, h);
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
        QImage img(reinterpret_cast<const uchar *>(data.Scan0), w, h,
                   static_cast<int>(data.Stride), QImage::Format_ARGB32);
        const QImage copy = img.copy();
        bitmap.UnlockBits(&data);
        painter->drawImage(0, 0, copy);
    }
}
