#pragma once

// The noise control swapper: one pill, one segment per mode.

#include "aap/battery.hpp"
#include "aap/packets.hpp"
#include "ui/dpi.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <Qt>

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace ui
{

class ModeSwitcher : public QWidget
{
public:
    explicit ModeSwitcher(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("modeSwitcher"));
        setAttribute(Qt::WA_StyledBackground, true);

        layout = new QHBoxLayout(this);

        group = new QButtonGroup(this);
        group->setExclusive(true);

        for (size_t i = 0; i < kModes.size(); ++i)
        {
            const aap::NoiseMode mode = kModes[i];

            auto *segment = new QPushButton(
                QString::fromStdString(std::string(aap::NoiseModeName(mode))), this
            );

            segment->setCheckable(true);
            segment->setCursor(Qt::PointingHandCursor);

            group->addButton(segment, static_cast<int>(i));
            layout->addWidget(segment);

            buttons[i] = segment;
        }

        QObject::connect(
            group, &QButtonGroup::idClicked,
            [this](int index)
            {
                if (handler)
                {
                    handler(kModes[static_cast<size_t>(index)]);
                }
            }
        );

        ApplyScale();
    }

    // Re-run when the window lands on a screen with a different scale.
    void ApplyScale()
    {
        const int padding = Px(kPadding);
        layout->setContentsMargins(padding, padding, padding, padding);
        layout->setSpacing(padding);
        setStyleSheet(StyleSheet());
    }

    void OnPicked(const std::function<void(aap::NoiseMode)> &callback) { handler = callback; }

    // A mode can only be sent over the control link, so the segments go grey
    // while the buds are merely advertising.
    void ShowLink(aap::LinkState link)
    {
        const bool up = link == aap::LinkState::Up;

        setEnabled(up);
        setToolTip(up ? QString{} : QStringLiteral("Connect the AirPods to change noise control"));

        for (QPushButton *segment : buttons)
        {
            segment->setCursor(up ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
    }

    // Driven by what the buds report, so the stem moves the selection too.
    void Show(aap::NoiseMode mode)
    {
        for (size_t i = 0; i < kModes.size(); ++i)
        {
            buttons[i]->setChecked(kModes[i] == mode);
        }
    }

private:
    static constexpr int kPadding = 5;
    static constexpr int kPillRadius = 15;
    static constexpr int kSegmentRadius = 12;
    static constexpr int kSegmentPaddingY = 5;
    static constexpr int kSegmentPaddingX = 12;

    static constexpr std::array<aap::NoiseMode, 4> kModes = {
        aap::NoiseMode::Off, aap::NoiseMode::NoiseCancellation, aap::NoiseMode::Transparency,
        aap::NoiseMode::Adaptive
    };

    // The px in here scale with the display like every other size does, so the
    // sheet is built rather than baked.
    static QString StyleSheet()
    {
        return QStringLiteral(R"(
#modeSwitcher { background: #3a3a3c; border-radius: %1px; }
QPushButton { border: none; border-radius: %2px; padding: %3px %4px; color: #e0e0e0; }
QPushButton:hover:!checked { background: rgba(255, 255, 255, 0.08); }
QPushButton:checked { background: #0a84ff; color: #ffffff; }
QPushButton:disabled { color: #7a7a7a; }
QPushButton:checked:disabled { background: #2c4a6e; }
)")
            .arg(Px(kPillRadius))
            .arg(Px(kSegmentRadius))
            .arg(Px(kSegmentPaddingY))
            .arg(Px(kSegmentPaddingX));
    }

    QHBoxLayout *layout = nullptr;
    QButtonGroup *group = nullptr;
    std::array<QPushButton *, kModes.size()> buttons{};
    std::function<void(aap::NoiseMode)> handler;
};

} // namespace ui
