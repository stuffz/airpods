#pragma once

// Owns the system tray item: its pixmap, tooltip and context menu.

#include "aap/battery.hpp"
#include "aap/packets.hpp"
#include "core/logger.hpp"
#include "ui/battery_icon.hpp"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QObject>
#include <QString>
#include <QSystemTrayIcon>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace ui
{

class TrayIcon
{
public:
    using ActivateHandler = std::function<void()>;

    TrayIcon() = default;
    ~TrayIcon() = default;

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    static bool Available() { return QSystemTrayIcon::isSystemTrayAvailable(); }

    void Show()
    {
        menu = std::make_unique<QMenu>();

        leftAction = menu->addAction(QString{});
        rightAction = menu->addAction(QString{});
        caseAction = menu->addAction(QString{});

        leftAction->setEnabled(false);
        rightAction->setEnabled(false);
        caseAction->setEnabled(false);

        menu->addSeparator();
        BuildNoiseMenu();

        menu->addSeparator();
        detailsAction = menu->addAction(QStringLiteral("Show details"));
        quitAction = menu->addAction(QStringLiteral("Quit"));

        icon = std::make_unique<QSystemTrayIcon>();

        // Showing a tray item that has no icon yet warns and leaves a gap until
        // the first report lands. An empty battery renders what Update() would
        // draw for one anyway: the glyph with an unfilled ring.
        icon->setIcon(BatteryIcon::Render(aap::Battery{}));
        icon->setContextMenu(menu.get());

        // Plasma's StatusNotifierItem delivers a plain left click as Trigger and
        // may never send DoubleClick, so both open the panel.
        QObject::connect(
            icon.get(), &QSystemTrayIcon::activated,
            [this](QSystemTrayIcon::ActivationReason reason)
            {
                LOG_DEBUG("tray activated, reason " + std::to_string(static_cast<int>(reason)));

                const bool opens =
                    reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger;

                if (opens && activateHandler)
                {
                    activateHandler();
                }
            }
        );

        icon->show();
    }

    void OnActivate(ActivateHandler handler) { activateHandler = std::move(handler); }

    void OnQuit(const std::function<void()> &handler)
    {
        QObject::connect(quitAction, &QAction::triggered, handler);
    }

    void OnShowDetails(const std::function<void()> &handler)
    {
        QObject::connect(detailsAction, &QAction::triggered, handler);
    }

    void OnNoiseMode(const std::function<void(aap::NoiseMode)> &handler) { noiseHandler = handler; }

    // Reflects what the buds report, so the tick marks follow the physical
    // stem as well as our own menu.
    void ShowNoiseMode(aap::NoiseMode mode)
    {
        for (const auto &entry : noiseActions)
        {
            entry.action->setChecked(entry.mode == mode);
        }
    }

    void Update(const aap::Battery &battery, bool connected)
    {
        if (!icon)
        {
            return;
        }

        icon->setIcon(BatteryIcon::Render(battery));

        SetEntry(leftAction, battery, aap::Component::Left);
        SetEntry(rightAction, battery, aap::Component::Right);
        SetEntry(caseAction, battery, aap::Component::Case);

        icon->setToolTip(QString::fromStdString(Tooltip(battery, connected)));

        // Modes go over the control link; without it the menu would only lie.
        noiseMenuAction->setEnabled(connected);
    }

private:
    struct NoiseEntry
    {
        QAction *action = nullptr;
        aap::NoiseMode mode = aap::NoiseMode::Off;
    };

    static constexpr std::array<aap::NoiseMode, 4> kNoiseModes = {
        aap::NoiseMode::Off, aap::NoiseMode::NoiseCancellation, aap::NoiseMode::Transparency,
        aap::NoiseMode::Adaptive
    };

    void BuildNoiseMenu()
    {
        QMenu *submenu = menu->addMenu(QStringLiteral("Noise control"));
        noiseMenuAction = submenu->menuAction();
#ifdef _WIN32
        noiseMenuAction->setVisible(false);
#endif

        noiseGroup = new QActionGroup(submenu);
        noiseGroup->setExclusive(true);

        for (size_t i = 0; i < kNoiseModes.size(); ++i)
        {
            const aap::NoiseMode mode = kNoiseModes[i];

            QAction *action =
                submenu->addAction(QString::fromStdString(std::string(aap::NoiseModeName(mode))));

            action->setCheckable(true);
            action->setActionGroup(noiseGroup);

            QObject::connect(
                action, &QAction::triggered,
                [this, mode]
                {
                    if (noiseHandler)
                    {
                        noiseHandler(mode);
                    }
                }
            );

            noiseActions[i] = NoiseEntry{action, mode};
        }
    }

    static void SetEntry(QAction *action, const aap::Battery &battery, aap::Component component)
    {
        const auto &state = battery.Get(component);

        action->setVisible(state.known);
        if (!state.known)
        {
            return;
        }

        action->setText(QString::fromStdString(Describe(battery, component)));
    }

    static std::string Describe(const aap::Battery &battery, aap::Component component)
    {
        const auto &state = battery.Get(component);

        return std::string(aap::Battery::Name(component)) + "  " +
               std::to_string(static_cast<int>(state.level)) + "%  " +
               std::string(aap::Battery::StatusName(state.status));
    }

    static std::string Tooltip(const aap::Battery &battery, bool connected)
    {
        if (!battery.HasReading())
        {
            return connected ? "AirPods connected" : "Waiting for AirPods battery readings";
        }

        std::string text;

        for (const aap::Component component :
             {aap::Component::Headset, aap::Component::Left, aap::Component::Right,
              aap::Component::Case})
        {
            if (!battery.Get(component).known)
            {
                continue;
            }

            if (!text.empty())
            {
                text += "\n";
            }

            text += Describe(battery, component);
        }

        return text;
    }

    std::unique_ptr<QSystemTrayIcon> icon;
    std::unique_ptr<QMenu> menu;
    QAction *leftAction = nullptr;
    QAction *rightAction = nullptr;
    QAction *caseAction = nullptr;
    QAction *detailsAction = nullptr;
    QAction *quitAction = nullptr;
    ActivateHandler activateHandler;
    std::function<void(aap::NoiseMode)> noiseHandler;
    QAction *noiseMenuAction = nullptr;
    QActionGroup *noiseGroup = nullptr;
    std::array<NoiseEntry, kNoiseModes.size()> noiseActions;
};

} // namespace ui
