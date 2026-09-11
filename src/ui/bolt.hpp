#pragma once

// The charging bolt, shared by the tray icon and the overview cells so the two
// never drift apart.

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <cstddef>
#include <iterator>

namespace ui
{

// Fills the bolt into the square box, white. Callers pick the size and place.
inline void DrawBolt(QPainter &painter, const QRectF &box)
{
    constexpr double kSource = 14.0;
    static constexpr double kPoints[][2] = {{7, 2},  {3, 8}, {6, 8}, {5, 12},
                                            {11, 6}, {8, 6}, {9, 2}};

    const double scale = box.width() / kSource;
    const QPointF origin = box.topLeft();

    QPainterPath bolt;
    for (size_t i = 0; i < std::size(kPoints); ++i)
    {
        const QPointF point(
            origin.x() + (kPoints[i][0] * scale), origin.y() + (kPoints[i][1] * scale)
        );

        if (i == 0)
        {
            bolt.moveTo(point);
            continue;
        }

        bolt.lineTo(point);
    }

    bolt.closeSubpath();

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(bolt);
}

} // namespace ui
