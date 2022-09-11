/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor.h"
#include "redirect.h"

#include "base/wayland/server.h"
#include "input/dbus/dbus.h"
#include "input/global_shortcuts_manager.h"
#include "input/platform.h"
#include "input/types.h"

#include <QPointF>
#include <Wrapland/Server/display.h>

namespace KWin::input::wayland
{

template<typename Base>
class platform : public input::platform<Base>
{
public:
    using type = platform<Base>;
    using space_t = typename Base::space_t;
    using redirect_t = wayland::redirect<type, space_t>;

    platform(Base& base)
        : input::platform<Base>(base)
        , xkb{xkb::manager<type>(this)}
    {
        this->config = kwinApp()->inputConfig();

        virtual_keyboard = waylandServer()->display->create_virtual_keyboard_manager_v1();
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;

    void setup_action_for_global_accel(QAction* /*action*/)
    {
    }

    void registerShortcut(QKeySequence const& /*shortcut*/, QAction* /*action*/)
    {
    }

    /**
     * @overload
     *
     * Like registerShortcut, but also connects QAction::triggered to the @p slot on @p receiver.
     * It's recommended to use this method as it ensures that the X11 timestamp is updated prior
     * to the @p slot being invoked. If not using this overload it's required to ensure that
     * registerShortcut is called before connecting to QAction's triggered signal.
     */
    template<typename T, typename Slot>
    void registerShortcut(const QKeySequence& shortcut, QAction* action, T* receiver, Slot slot)
    {
        registerShortcut(shortcut, action);
        QObject::connect(action, &QAction::triggered, receiver, slot);
    }

    void install_shortcuts()
    {
        this->shortcuts = std::make_unique<input::global_shortcuts_manager>();
        this->shortcuts->init();
        setup_touchpad_shortcuts();
    }

    void update_keyboard_leds(input::keyboard_leds leds)
    {
        for (auto& keyboard : this->keyboards) {
            if (keyboard->control) {
                keyboard->control->update_leds(leds);
            }
        }
    }

    void toggle_touchpads()
    {
        auto changed{false};
        touchpads_enabled = !touchpads_enabled;

        for (auto& pointer : this->pointers) {
            if (!pointer->control) {
                continue;
            }
            auto& ctrl = pointer->control;
            if (!ctrl->is_touchpad()) {
                continue;
            }

            auto old_enabled = ctrl->is_enabled();
            ctrl->set_enabled(touchpads_enabled);

            if (old_enabled != ctrl->is_enabled()) {
                changed = true;
            }
        }

        if (changed) {
            dbus::inform_touchpad_toggle(touchpads_enabled);
        }
    }

    void enable_touchpads()
    {
        if (touchpads_enabled) {
            return;
        }
        toggle_touchpads();
    }

    void disable_touchpads()
    {
        if (!touchpads_enabled) {
            return;
        }
        toggle_touchpads();
    }

    std::unique_ptr<Wrapland::Server::virtual_keyboard_manager_v1> virtual_keyboard;

    redirect_t* redirect{nullptr};

    input::xkb::manager<type> xkb;
    std::unique_ptr<dbus::device_manager<type>> dbus;

private:
    void setup_touchpad_shortcuts()
    {
        auto toggle_action = new QAction(this->qobject.get());
        auto on_action = new QAction(this->qobject.get());
        auto off_action = new QAction(this->qobject.get());

        constexpr auto const component{"kcm_touchpad"};

        toggle_action->setObjectName(QStringLiteral("Toggle Touchpad"));
        toggle_action->setProperty("componentName", component);
        on_action->setObjectName(QStringLiteral("Enable Touchpad"));
        on_action->setProperty("componentName", component);
        off_action->setObjectName(QStringLiteral("Disable Touchpad"));
        off_action->setProperty("componentName", component);

        KGlobalAccel::self()->setDefaultShortcut(toggle_action,
                                                 QList<QKeySequence>{Qt::Key_TouchpadToggle});
        KGlobalAccel::self()->setShortcut(toggle_action,
                                          QList<QKeySequence>{Qt::Key_TouchpadToggle});
        KGlobalAccel::self()->setDefaultShortcut(on_action,
                                                 QList<QKeySequence>{Qt::Key_TouchpadOn});
        KGlobalAccel::self()->setShortcut(on_action, QList<QKeySequence>{Qt::Key_TouchpadOn});
        KGlobalAccel::self()->setDefaultShortcut(off_action,
                                                 QList<QKeySequence>{Qt::Key_TouchpadOff});
        KGlobalAccel::self()->setShortcut(off_action, QList<QKeySequence>{Qt::Key_TouchpadOff});

        registerShortcut(Qt::Key_TouchpadToggle, toggle_action);
        registerShortcut(Qt::Key_TouchpadOn, on_action);
        registerShortcut(Qt::Key_TouchpadOff, off_action);

        QObject::connect(toggle_action, &QAction::triggered, this->qobject.get(), [this] {
            toggle_touchpads();
        });
        QObject::connect(
            on_action, &QAction::triggered, this->qobject.get(), [this] { enable_touchpads(); });
        QObject::connect(
            off_action, &QAction::triggered, this->qobject.get(), [this] { disable_touchpads(); });
    }

    bool touchpads_enabled{true};
};

template<typename Input>
void add_dbus(Input* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager<Input>>(*platform);
}

}
