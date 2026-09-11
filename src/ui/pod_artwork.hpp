#pragma once

// Noun Project icons by Abbidzart, CC BY 3.0; see ATTRIBUTION.md.

#include "ui/airpods_glyph.hpp"

#include <QByteArray>
#include <QPaintEvent>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QSvgRenderer>
#include <QWidget>
#include <Qt>
#include <QtLogging>

namespace ui
{

class PodArtwork : public QWidget
{
public:
    static PodArtwork *Left() { return new PodArtwork(QByteArray(kRight), Facing::Mirrored); }

    static PodArtwork *Right() { return new PodArtwork(QByteArray(kRight), Facing::Original); }

    static PodArtwork *Case()
    {
        return new PodArtwork(
            AirPodsGlyph().replace("<path ", "<path fill=\"#e0e0e0\" "), Facing::Original
        );
    }

private:
    enum class Facing
    {
        Original,
        Mirrored
    };

    PodArtwork(const QByteArray &svg, Facing direction) : renderer(svg), facing(direction)
    {
        if (!renderer.isValid())
        {
            qFatal("Invalid embedded AirPods artwork");
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (facing == Facing::Mirrored)
        {
            painter.translate(width(), 0);
            painter.scale(-1, 1);
        }

        QSizeF size = renderer.viewBoxF().size();
        size.scale(QSizeF(width(), height()), Qt::KeepAspectRatio);
        const QRectF bounds(
            (width() - size.width()) / 2, (height() - size.height()) / 2, size.width(),
            size.height()
        );
        renderer.render(&painter, bounds);
    }

private:
    QSvgRenderer renderer;
    Facing facing;

    static constexpr const char *kRight =
        R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="34 0 64 64" fill="#e0e0e0">
  <path d="m68.641 61.613h3.4102c1.8594 0 3.3906-1.3867 3.625-3.1797h-10.66c0.23438 1.793 1.7656 3.1797 3.625 3.1797z"/>
  <path d="m83.926 7.6094c-4.918-6.4453-14.129-7.6836-20.574-2.7656l-5.1211 3.9102c-1.6562 1.2656-2.9648 2.8125-3.918 4.5234 0.66406-0.035156 1.3359-0.011719 2.0078 0.078125 2.8086 0.375 5.3008 1.8242 7.0195 4.0781l3.2031 4.1992c1.7188 2.2539 2.457 5.0391 2.082 7.8477-0.35156 2.625-1.6406 4.9727-3.6445 6.6719v21.445h10.73v-25.27c0.10937-0.078125 0.21875-0.16016 0.32812-0.24219l5.1211-3.9102c6.4414-4.918 7.6797-14.129 2.7617-20.574zm-6.8711 6.4492-4.4297 2.8789c-1.1367 0.73828-2.6562 0.41406-3.3906-0.71875-0.73828-1.1367-0.41406-2.6562 0.71875-3.3906l4.4297-2.8789c1.1367-0.73828 2.6562-0.41406 3.3906 0.71875 0.73828 1.1367 0.41406 2.6562-0.71875 3.3906z"/>
  <path d="m65.715 22.266-3.2031-4.1992c-3.207-4.1992-9.2109-5.0039-13.41-1.8008-4.9414 3.7734-5.8906 10.836-2.1172 15.777l1.1562 1.5117c3.7734 4.9414 10.836 5.8906 15.777 2.1172 4.1992-3.207 5.0039-9.2109 1.8008-13.41zm-10.973 11.176c-1.1992 0.78125-3.4727-0.58203-5.0742-3.0469-1.6016-2.4609-1.9219-5.0898-0.72266-5.8711 1.1992-0.78125 3.4727 0.58203 5.0742 3.0469 1.6016 2.4609 1.9219 5.0898 0.72266 5.8711z"/>
</svg>)SVG";
};

} // namespace ui
