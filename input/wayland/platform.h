/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor.h"
#include "input_method.h"
#include "redirect.h"

#include "base/platform.h"
#include "base/wayland/output_helpers.h"
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

        input_method = std::make_unique<wayland::input_method<type>>(*this, waylandServer());
        virtual_keyboard = waylandServer()->display->create_virtual_keyboard_manager_v1();

        QObject::connect(&base, &Base::output_added, this->qobject.get(), [this] {
            base::wayland::check_outputs_on(this->base, dpms_filter);
        });
        QObject::connect(&base, &Base::output_removed, this->qobject.get(), [this] {
            base::wayland::check_outputs_on(this->base, dpms_filter);
        });
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

    /**
     * Starts an interactive window selection process.
     *
     * Once the user selected a window the @p callback is invoked with the selected Toplevel as
     * argument. In case the user cancels the interactive window selection or selecting a window is
     * currently not possible (e.g. screen locked) the @p callback is invoked with a @c nullptr
     * argument.
     *
     * During the interactive window selection the cursor is turned into a crosshair cursor unless
     * @p cursorName is provided. The argument @p cursorName is a QByteArray instead of
     * Qt::CursorShape to support the "pirate" cursor for kill window which is not wrapped by
     * Qt::CursorShape.
     *
     * @param callback The function to invoke once the interactive window selection ends
     * @param cursorName The optional name of the cursor shape to use, default is crosshair
     */
    void
    start_interactive_window_selection(std::function<void(typename space_t::window_t*)> callback,
                                       QByteArray const& cursorName = QByteArray())
    {
        if (!redirect) {
            callback(nullptr);
            return;
        }
        redirect->startInteractiveWindowSelection(callback, cursorName);
    }

    /**
     * Starts an interactive position selection process.
     *
     * Once the user selected a position on the screen the @p callback is invoked with
     * the selected point as argument. In case the user cancels the interactive position selection
     * or selecting a position is currently not possible (e.g. screen locked) the @p callback
     * is invoked with a point at @c -1 as x and y argument.
     *
     * During the interactive window selection the cursor is turned into a crosshair cursor.
     *
     * @param callback The function to invoke once the interactive position selection ends
     */
    void start_interactive_position_selection(std::function<void(QPoint const&)> callback)
    {
        if (!redirect) {
            callback(QPoint(-1, -1));
            return;
        }
        redirect->startInteractivePositionSelection(callback);
    }

    void turn_outputs_on()
    {
        base::wayland::turn_outputs_on(this->base, dpms_filter);
    }

    void warp_pointer(QPointF const& pos, uint32_t time)
    {
        if (this->pointers.empty()) {
            return;
        }

        redirect->pointer->processMotion(pos, time, this->pointers.front());
    }

    std::unique_ptr<wayland::input_method<type>> input_method;
    std::unique_ptr<Wrapland::Server::virtual_keyboard_manager_v1> virtual_keyboard;
    std::unique_ptr<input::dpms_filter<type, redirect_t>> dpms_filter;

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
