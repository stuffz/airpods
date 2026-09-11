#pragma once

// Display scaling for the px sizes the UI is built from.
//
// Qt normally carries the monitor scale in the device pixel ratio and leaves
// the logical DPI at 96, so a px size already comes out the right physical
// size and nothing here has to do anything. It does not always: X11 sessions
// that scale through the font DPI, and anything running with
// QT_ENABLE_HIGHDPI_SCALING=0, report a device pixel ratio of 1 and a logical
// DPI of 96*scale instead, and raw px then render scale-times too small.
//
// Folding that ratio into every px size covers both cases, which is the point:
// on the same 150% monitor the Linux and Windows windows came up visibly
// different sizes because the two platforms take different routes to the same
// scale factor.

#include <QFont>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>

#include <algorithm>
#include <cmath>

namespace ui
{

namespace detail
{

inline constexpr double kBaseDpi = 96.0;
inline constexpr double kMaxScale = 4.0;

inline QPointer<QScreen> &DpiScreen()
{
    static QPointer<QScreen> screen;
    return screen;
}

} // namespace detail

// The screen sizes are computed against. It is not always the primary: with a
// 150% primary and a 100% secondary, sizing everything against the primary
// leaves the window oversized the moment it is dragged across. Pass nullptr to
// fall back to the primary screen.
inline void SetDpiScreen(QScreen *screen)
{
    detail::DpiScreen() = screen;
}

// 1.0 when Qt applies the display scaling itself, otherwise the factor px
// sizes have to be multiplied by to come out the intended physical size.
//
// The device pixel ratio is divided back out rather than assumed to be 1: what
// a px is finally worth is ratio * (logical dpi / 96), so dividing leaves this
// returning only the part Qt has not already applied. A platform that reported
// the scale in both places at once would otherwise be scaled twice.
inline double DpiScale()
{
    const QScreen *screen =
        detail::DpiScreen() ? detail::DpiScreen().data() : QGuiApplication::primaryScreen();

    if (screen == nullptr)
    {
        return 1.0;
    }

    const double dpi = screen->logicalDotsPerInchY();
    const double ratio = screen->devicePixelRatio();
    if (dpi <= 0.0 || ratio <= 0.0)
    {
        return 1.0;
    }

    return std::clamp((dpi / detail::kBaseDpi) / ratio, 1.0, detail::kMaxScale);
}

// A px size through that correction. Nothing lands on zero: a scaled-down
// border of 0 draws nothing at all, where 1 draws the hairline that was meant.
inline int Px(int px)
{
    return std::max(1, static_cast<int>(std::lround(px * DpiScale())));
}

// The base font, in px rather than pt. Points vary with the screen DPI while
// every other size in the app is in pixels, and the platform defaults differ
// besides — Segoe UI 9pt on Windows against whatever fontconfig picks — which
// is the other half of why the two windows did not match.
inline constexpr int kBaseFontPx = 13;
inline constexpr int kHeadingFontPx = 15;

// Pins the application font so labels, and the widths the layout derives from
// them, do not depend on which platform default happens to be installed.
// Called at startup and again whenever the window changes screen.
inline void ApplyBaseFont()
{
    QFont font = QGuiApplication::font();
    // px, not pt: every other size in the app is in px, and the two have to
    // agree or the window grows around text it did not size.
    font.setPixelSize(Px(kBaseFontPx));
    QGuiApplication::setFont(font);
}

} // namespace ui
