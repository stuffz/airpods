#pragma once

// One component of the overview: its icon above its battery.

#include "aap/battery.hpp"
#include "ui/battery_indicator.hpp"
#include "ui/dpi.hpp"
#include "ui/pod_artwork.hpp"

#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

namespace ui
{

class PodColumn : public QWidget
{
public:
    PodColumn(PodArtwork *art, const QString &badge, QWidget *parent = nullptr) : QWidget(parent)
    {
        artwork = art;
        indicator = new BatteryIndicator(this);

        layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(art, 0, Qt::AlignHCenter);
        layout->addWidget(indicator, 0, Qt::AlignHCenter);

        ApplyScale();

        badgeText = badge;

        // Nothing is known until a report arrives, and an empty column with a
        // dash under it reads as a component that exists but is dead.
        hide();
    }

    void SetState(const aap::ComponentBattery &state)
    {
        indicator->SetState(state, badgeText);
        setVisible(state.known);
    }

    // Re-run when the window lands on a screen with a different scale.
    void ApplyScale()
    {
        layout->setSpacing(Px(kSpacing));
        artwork->setFixedSize(Px(kImageSize), Px(kImageSize));
        indicator->ApplyScale();
    }

private:
    static constexpr int kImageSize = 72;
    static constexpr int kSpacing = 5;

    QVBoxLayout *layout = nullptr;
    PodArtwork *artwork = nullptr;
    BatteryIndicator *indicator = nullptr;
    QString badgeText;
};

} // namespace ui
