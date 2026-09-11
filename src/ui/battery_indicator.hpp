#pragma once

// A horizontal battery pill with an L/R badge and the level beneath it.

#include "aap/battery.hpp"
#include "ui/bolt.hpp"
#include "ui/dpi.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <Qt>

#include <cstdint>

namespace ui
{

class BatteryIndicator : public QWidget
{
public:
    explicit BatteryIndicator(QWidget *parent = nullptr) : QWidget(parent) { ApplyScale(); }

    // The pill is drawn in a fixed kWidth x kHeight space and the painter is
    // scaled to fill whatever that space grew to, so the display scale only
    // has to be applied here.
    void ApplyScale() { setFixedSize(Px(kWidth), Px(kHeight)); }

    void SetState(const aap::ComponentBattery &value, const QString &badgeText)
    {
        state = value;
        badge = badgeText;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(DpiScale(), DpiScale());

        DrawCell(painter);
        DrawCaption(painter);
    }

private:
    static constexpr int kWidth = 86;
    static constexpr int kHeight = 44;

    static constexpr double kCellWidth = 34.0;
    static constexpr double kCellHeight = 17.0;
    static constexpr double kCellTop = 2.0;
    static constexpr double kCellRadius = 3.5;
    static constexpr double kBorderWidth = 1.5;
    static constexpr double kFillInset = 2.5;
    static constexpr double kBoltInset = 2.0;

    static constexpr double kTerminalWidth = 2.5;
    static constexpr double kTerminalHeight = 8.0;
    static constexpr double kTerminalRadius = 1.0;

    static constexpr int kCaptionTop = 24;
    static constexpr int kCaptionHeight = 18;
    static constexpr int kBadgeSize = 16;
    static constexpr int kBadgeGap = 5;
    static constexpr int kCaptionFontPx = 12;
    static constexpr int kBadgeFontPx = 10;

    static constexpr double kFullLevel = 100.0;
    static constexpr uint8_t kLowLevel = 20;
    static constexpr uint8_t kMidLevel = 50;

    void DrawCell(QPainter &painter) const
    {
        // kWidth, not width(): the painter is scaled, so the drawing space is
        // the unscaled one the constants were measured in.
        const double left = (kWidth - (kCellWidth + kTerminalWidth)) / 2;
        const QRectF body(left, kCellTop, kCellWidth, kCellHeight);

        QPen border(BorderColor());
        border.setWidthF(kBorderWidth);

        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(body, kCellRadius, kCellRadius);

        painter.setPen(Qt::NoPen);
        painter.setBrush(BorderColor());
        painter.drawRoundedRect(
            QRectF(
                body.right(), kCellTop + ((kCellHeight - kTerminalHeight) / 2), kTerminalWidth,
                kTerminalHeight
            ),
            kTerminalRadius, kTerminalRadius
        );

        if (!state.known)
        {
            return;
        }

        const double track = kCellWidth - (kFillInset * 2);
        const double filled = track * (state.level / kFullLevel);

        painter.setBrush(LevelColor());
        painter.drawRoundedRect(
            QRectF(
                left + kFillInset, kCellTop + kFillInset, filled, kCellHeight - (kFillInset * 2)
            ),
            kCellRadius / 2, kCellRadius / 2
        );

        // Drawn over the fill rather than beside it: at this size there is no
        // room next to the cell, and the bolt is what tells charging from a
        // full pod.
        if (state.status == aap::ChargeStatus::Charging)
        {
            const double size = body.height() - kBoltInset;
            DrawBolt(
                painter,
                QRectF(body.center().x() - (size / 2), body.center().y() - (size / 2), size, size)
            );
        }
    }

    void DrawCaption(QPainter &painter) const
    {
        const QString text =
            state.known ? QString::number(state.level) + "%" : QStringLiteral("--");

        QFont captionFont = painter.font();
        captionFont.setPixelSize(kCaptionFontPx);
        painter.setFont(captionFont);

        // QFontMetrics off the font rather than the painter: the painter is
        // scaled, and the layout below is in the unscaled drawing space.
        const int textWidth = QFontMetrics(captionFont).horizontalAdvance(text);
        const bool hasBadge = !badge.isEmpty();
        const int total = textWidth + (hasBadge ? kBadgeSize + kBadgeGap : 0);
        int cursor = (kWidth - total) / 2;

        if (hasBadge)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(BorderColor());
            painter.drawEllipse(cursor, kCaptionTop, kBadgeSize, kBadgeSize);

            QFont badgeFont = painter.font();
            badgeFont.setPixelSize(kBadgeFontPx);
            painter.setFont(badgeFont);
            painter.setPen(palette().color(QPalette::Window));
            painter.drawText(
                QRect(cursor, kCaptionTop, kBadgeSize, kBadgeSize), Qt::AlignCenter, badge
            );

            painter.setFont(captionFont);
            cursor += kBadgeSize + kBadgeGap;
        }

        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(
            QRect(cursor, kCaptionTop, textWidth, kCaptionHeight), Qt::AlignVCenter | Qt::AlignLeft,
            text
        );
    }

    QColor LevelColor() const
    {
        if (state.status == aap::ChargeStatus::Charging)
        {
            return {0x30, 0xd1, 0x58};
        }

        if (state.level <= kLowLevel)
        {
            return {0xff, 0x45, 0x3a};
        }

        if (state.level <= kMidLevel)
        {
            return {0xff, 0xd6, 0x0a};
        }

        return {0x30, 0xd1, 0x58};
    }

    QColor BorderColor() const { return palette().color(QPalette::ButtonText); }

    aap::ComponentBattery state;
    QString badge;
};

} // namespace ui
