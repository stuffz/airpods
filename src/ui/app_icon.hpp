#pragma once

// The application icon, drawn from the same glyph as the tray.

#include "ui/airpods_glyph.hpp"

#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSvgRenderer>
#include <Qt>

#include <array>

namespace ui
{

// Several sizes so window managers and task switchers pick a sharp one rather
// than rescaling a single pixmap.
inline QIcon AppIcon()
{
    static constexpr std::array<int, 4> kSizes = {32, 48, 64, 128};

    QSvgRenderer renderer(AirPodsAppIcon());
    QIcon icon;

    if (!renderer.isValid())
    {
        return icon;
    }

    for (const int size : kSizes)
    {
        QPixmap canvas(size, size);
        canvas.fill(Qt::transparent);

        // Square artwork with its own colours, so it is drawn as it is rather
        // than fitted and tinted the way the tray glyph has to be.
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0, 0, size, size));
        painter.end();

        icon.addPixmap(canvas);
    }

    return icon;
}

} // namespace ui
