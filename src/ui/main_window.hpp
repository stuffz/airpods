#pragma once

// The overview window: every component that is reporting, above the noise
// control swapper.

#include "aap/battery.hpp"
#include "aap/packets.hpp"
#include "core/logger.hpp"
#include "ui/dpi.hpp"
#include "ui/mode_switcher.hpp"
#include "ui/pod_artwork.hpp"
#include "ui/pod_column.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMoveEvent>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

#include <cmath>
#include <functional>
#include <string>

namespace ui
{

class MainWindow : public QWidget
{
public:
    explicit MainWindow(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowTitle(QStringLiteral("AirPods"));
        setObjectName(QStringLiteral("mainWindow"));
        setAttribute(Qt::WA_StyledBackground, true);

        layout = new QVBoxLayout(this);

        heading = new QLabel(this);
        heading->setObjectName(QStringLiteral("mainHeading"));
        heading->setAlignment(Qt::AlignCenter);

        pods = new QHBoxLayout;
        pods->addStretch();

        left = new PodColumn(PodArtwork::Left(), QStringLiteral("L"), this);
        right = new PodColumn(PodArtwork::Right(), QStringLiteral("R"), this);
        headset = new PodColumn(PodArtwork::Left(), QString{}, this);
        podCase = new PodColumn(PodArtwork::Case(), QString{}, this);

        for (PodColumn *column : {left, right, headset, podCase})
        {
            pods->addWidget(column);
        }

        pods->addStretch();

        switcher = new ModeSwitcher(this);
#ifdef _WIN32
        switcher->hide();
#endif

        age = new QLabel(this);
        age->setObjectName(QStringLiteral("mainAge"));
        age->setAlignment(Qt::AlignCenter);
        age->hide();

        empty = new QLabel(QStringLiteral("Waiting for the AirPods…"), this);
        empty->setObjectName(QStringLiteral("mainEmpty"));
        empty->setAlignment(Qt::AlignCenter);

        layout->addWidget(heading);
        layout->addLayout(pods);
        layout->addWidget(age);
        layout->addWidget(empty);
        layout->addWidget(switcher, 0, Qt::AlignHCenter);

        ApplyScale();
    }

    void OnModePicked(const std::function<void(aap::NoiseMode)> &callback)
    {
        switcher->OnPicked(callback);
    }

    void ShowMode(aap::NoiseMode mode) { switcher->Show(mode); }

    void ShowLink(aap::LinkState link) { switcher->ShowLink(link); }

    void Update(const aap::Battery &battery, const std::string &title)
    {
        heading->setText(QString::fromStdString(title));

        left->SetState(battery.Get(aap::Component::Left));
        right->SetState(battery.Get(aap::Component::Right));
        headset->SetState(battery.Get(aap::Component::Headset));

        // The case only reports while its lid is open, so the column comes and
        // goes with it rather than sitting there empty.
        podCase->SetState(battery.Get(aap::Component::Case));

        empty->setVisible(!battery.HasReading());
    }

    // Empty text hides the line: a live reading should not carry an age.
    void ShowAge(const std::string &text)
    {
        age->setText(QString::fromStdString(text));
        age->setVisible(!text.empty());
    }

    void Toggle()
    {
        if (isVisible())
        {
            hide();
            return;
        }

        show();
        SyncScreen();
        raise();
        activateWindow();
    }

protected:
    // Catches the window being dragged from a 150% monitor to a 100% one,
    // where every px size it was built from now means something else.
    void moveEvent(QMoveEvent *event) override
    {
        QWidget::moveEvent(event);
        SyncScreen();
    }

private:
    static constexpr int kMargin = 18;
    static constexpr int kSpacing = 16;
    static constexpr int kColumnGap = 18;
    static constexpr double kScaleEpsilon = 0.001;

    void SyncScreen()
    {
        QScreen *current = screen();
        if (current == trackedScreen)
        {
            return;
        }

        trackedScreen = current;
        SetDpiScreen(current);

        // The numbers behind a window that comes up the wrong size: a platform
        // carrying the scale in the device pixel ratio reports 96 logical DPI
        // and a ratio of 1.5, one scaling through the font DPI reports 144 and
        // 1. Both must end at the same window size.
        if (current != nullptr)
        {
            std::string report = "screen " + current->name().toStdString();
            report += ", logical dpi " + std::to_string(current->logicalDotsPerInchY());
            report += ", device pixel ratio " + std::to_string(current->devicePixelRatio());
            report += ", px scale " + std::to_string(DpiScale());
            report += ", window " + std::to_string(width()) + "x" + std::to_string(height());
            LOG_DEBUG(report);
        }

        const bool rescale = std::abs(DpiScale() - appliedScale) >= kScaleEpsilon;

        // Only when the px sizes themselves have to change. A platform that
        // carries the scale in its device pixel ratio reports the same 1.0 on
        // every monitor and the window is already right; rebuilding anyway
        // would resize it for nothing, which reads as a jump.
        //
        // The first time through is the exception. The constructor's font is
        // subject to the same posted-event delay described below, so the window
        // is shown before the layout can measure it and comes up at the default
        // 640x480 -- and the first move to another screen, which is the first
        // time anything sizes it properly, looks like an unprompted resize.
        if (!rescale && sized)
        {
            return;
        }

        if (rescale)
        {
            ApplyScale();
        }

        sized = true;
        ResizeToScale();
    }

    // Sizing here and now would measure the screen we just left. ApplyScale()
    // installs a new application font, and the FontChange events that clear
    // each widget's cached size hint are posted rather than sent (QTBUG-55449:
    // widgets cache size hints and do not invalidate them on a DPI change), so
    // the hints are still the old screen's until the event loop turns.
    //
    // That asymmetry is the bug it fixes: growing worked, because a larger
    // stale hint is simply accepted, but shrinking did not -- the stale larger
    // hints held the window's minimum up, so moving off the 150% monitor left
    // it 1.5x too big and every round trip kept it there.
    void ResizeToScale()
    {
        QTimer::singleShot(
            0, this,
            [this]
            {
                for (QWidget *child : findChildren<QWidget *>())
                {
                    child->updateGeometry();
                }

                layout->invalidate();
                layout->activate();
                updateGeometry();
                resize(sizeHint());

                LOG_DEBUG(
                    "rescaled to " + std::to_string(width()) + "x" + std::to_string(height())
                );
            }
        );
    }

    void ApplyScale()
    {
        appliedScale = DpiScale();
        ApplyBaseFont();

        const int margin = Px(kMargin);
        layout->setContentsMargins(margin, margin, margin, margin);
        layout->setSpacing(Px(kSpacing));
        pods->setSpacing(Px(kColumnGap));

        for (PodColumn *column : {left, right, headset, podCase})
        {
            column->ApplyScale();
        }

        switcher->ApplyScale();
        setStyleSheet(StyleSheet());
    }

    // The px in here scale with the display like every other size does, so the
    // sheet is built rather than baked.
    static QString StyleSheet()
    {
        return QStringLiteral(R"(
#mainWindow { background: #1f1f21; }
QLabel { color: #e0e0e0; }
#mainHeading { font-size: %1px; font-weight: bold; }
#mainEmpty { color: #8a8a8a; }
#mainAge { color: #8a8a8a; }
)")
            .arg(Px(kHeadingFontPx));
    }

    QVBoxLayout *layout = nullptr;
    QHBoxLayout *pods = nullptr;
    QPointer<QScreen> trackedScreen;
    double appliedScale = 0.0;
    bool sized = false;
    QLabel *heading = nullptr;
    QLabel *empty = nullptr;
    QLabel *age = nullptr;
    PodColumn *left = nullptr;
    PodColumn *right = nullptr;
    PodColumn *headset = nullptr;
    PodColumn *podCase = nullptr;
    ModeSwitcher *switcher = nullptr;
};

} // namespace ui
