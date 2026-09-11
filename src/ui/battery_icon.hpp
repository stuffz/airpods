#pragma once

// Draws the tray pixmap: the AirPods glyph inside a ring that fills with charge.
//
// The ring speaks for the pods only. The case has its own battery and its own
// charger, and neither is what you want to know while the buds are in your
// ears; the case shows up in the menu and the overview instead.

#include "aap/battery.hpp"
#include "ui/airpods_glyph.hpp"
#include "ui/bolt.hpp"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSvgRenderer>
#include <Qt>

#include <cstdint>

namespace ui
{

// Rendered rather than loaded from PNGs: two pods means two levels, and one
// pixmap per level pair is a combinatorial mess of files to ship.
class BatteryIcon
{
public:
    static QIcon Render(const aap::Battery &battery)
    {
        QPixmap canvas(kCanvasWidth, kCanvasHeight);
        canvas.fill(Qt::transparent);

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const aap::ComponentBattery shown = Worst(battery);

        DrawGlyph(painter);
        DrawRing(painter, shown);

        if (shown.known && shown.status == aap::ChargeStatus::Charging)
        {
            DrawBoltBadge(painter);
        }

        painter.end();
        return QIcon(canvas);
    }

private:
    static constexpr int kCanvasWidth = 64;
    static constexpr int kCanvasHeight = 64;

    static constexpr double kGlyphLeft = 14.0;
    static constexpr double kGlyphWidth = 36.0;

    // Measured to the stroke centre, so the outer edge stays inside the canvas.
    static constexpr double kRingRadius = 27.5;
    static constexpr double kRingWidth = 8.0;

    // QPainter angles are sixteenths of a degree, counter-clockwise from three
    // o'clock. The arc starts at twelve and runs clockwise like a clock face.
    static constexpr int kSixteenths = 16;
    static constexpr int kArcStart = 90 * kSixteenths;
    static constexpr int kFullTurn = 360 * kSixteenths;

    // Bottom right, overlapping the ring, on a disc so the bolt reads over
    // whatever colour the ring has there.
    static constexpr double kBadgeRadius = 13.5;
    static constexpr double kBadgeCentre = kCanvasWidth - kBadgeRadius - 1.0;
    static constexpr double kBadgeBoltSize = 20.0;

    static constexpr double kFullLevel = 100.0;

    // Solid red up to kLowLevel, then the hue slides through amber to green.
    // A hue sweep along the arc was rejected: a full ring would carry red.
    static constexpr uint8_t kLowLevel = 20;
    static constexpr int kRedHue = 0;
    static constexpr int kGreenHue = 120;
    static constexpr int kSaturation = 200;
    static constexpr int kValue = 220;

    // One ring cannot show two pods, so it shows the one that will die first.
    static aap::ComponentBattery Worst(const aap::Battery &battery)
    {
        const auto &left = battery.Get(aap::Component::Left);
        const auto &right = battery.Get(aap::Component::Right);
        const auto &headset = battery.Get(aap::Component::Headset);

        if (headset.known)
        {
            return headset;
        }

        if (left.known && right.known)
        {
            return left.level <= right.level ? left : right;
        }

        return left.known ? left : right;
    }

    static void DrawGlyph(QPainter &painter)
    {
        QSvgRenderer renderer(AirPodsGlyph());
        if (!renderer.isValid())
        {
            return;
        }

        const QRectF box = renderer.viewBoxF();
        const double height = kGlyphWidth * (box.height() / box.width());
        const QRectF target(kGlyphLeft, (kCanvasHeight - height) / 2, kGlyphWidth, height);

        // The artwork is flat black, so it is rendered as a mask and filled
        // with the tray colour rather than recoloured in the SVG itself.
        QPixmap layer(kCanvasWidth, kCanvasHeight);
        layer.fill(Qt::transparent);

        QPainter shape(&layer);
        shape.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&shape, target);
        shape.setCompositionMode(QPainter::CompositionMode_SourceIn);
        shape.fillRect(layer.rect(), GlyphColor());
        shape.end();

        painter.drawPixmap(0, 0, layer);
    }

    static void DrawRing(QPainter &painter, const aap::ComponentBattery &state)
    {
        const QRectF bounds(
            (kCanvasWidth / 2.0) - kRingRadius, (kCanvasHeight / 2.0) - kRingRadius,
            kRingRadius * 2, kRingRadius * 2
        );

        QPen pen(UnknownColor());
        pen.setWidthF(kRingWidth);
        pen.setCapStyle(Qt::RoundCap);

        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(bounds);

        if (!state.known || state.level == 0)
        {
            return;
        }

        const int span = static_cast<int>(kFullTurn * (state.level / kFullLevel));

        pen.setColor(LevelColor(state));
        painter.setPen(pen);
        painter.drawArc(bounds, kArcStart, -span);
    }

    static void DrawBoltBadge(QPainter &painter)
    {
        const QPointF centre(kBadgeCentre, kBadgeCentre);

        painter.setPen(Qt::NoPen);
        painter.setBrush(BadgeColor());
        painter.drawEllipse(centre, kBadgeRadius, kBadgeRadius);

        const double half = kBadgeBoltSize / 2;
        DrawBolt(
            painter, QRectF(centre.x() - half, centre.y() - half, kBadgeBoltSize, kBadgeBoltSize)
        );
    }

    static QColor LevelColor(const aap::ComponentBattery &state)
    {
        if (state.level <= kLowLevel)
        {
            return QColor::fromHsv(kRedHue, kSaturation, kValue);
        }

        const double above = (state.level - kLowLevel) / (kFullLevel - kLowLevel);
        const int hue = kRedHue + static_cast<int>((kGreenHue - kRedHue) * above);

        return QColor::fromHsv(hue, kSaturation, kValue);
    }

    static QColor GlyphColor() { return {0xe0, 0xe0, 0xe0}; }

    static QColor UnknownColor() { return {0x61, 0x61, 0x61}; }

    static QColor BadgeColor() { return {0x21, 0x21, 0x21}; }
};

} // namespace ui
