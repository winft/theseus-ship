/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "input_method.h"
#include "pointer_redirect.h"
#include "redirect.h"

#include "base/backend/wlroots/platform.h"
#include "base/platform.h"
#include "base/wayland/output_helpers.h"
#include "base/wayland/server.h"
#include "input/dbus/dbus.h"
#include "input/dbus/device_manager.h"
#include "input/filters/dpms.h"
#include "input/global_shortcuts_manager.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/switch.h"
#include "input/touch.h"
#include "main.h"

#include <KGlobalAccel>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/virtual_keyboard_v1.h>

namespace KWin::input::wayland
{

platform::platform(base::wayland::platform const& base)
    : base{base}
{
    config = kwinApp()->inputConfig();

    input_method = std::make_unique<wayland::input_method>(*this, waylandServer());
    virtual_keyboard = waylandServer()->display->create_virtual_keyboard_manager_v1();

    QObject::connect(&base, &base::backend::wlroots::platform::output_added, this, [this] {
        base::wayland::check_outputs_on(this->base, dpms_filter);
    });
    QObject::connect(&base, &base::backend::wlroots::platform::output_removed, this, [this] {
        base::wayland::check_outputs_on(this->base, dpms_filter);
    });
}

platform::~platform() = default;

void platform::install_shortcuts()
{
    shortcuts = std::make_unique<input::global_shortcuts_manager>();
    shortcuts->init();
    setup_touchpad_shortcuts();
}

void platform::update_keyboard_leds(input::keyboard_leds leds)
{
    for (auto& keyboard : keyboards) {
        if (keyboard->control) {
            keyboard->control->update_leds(leds);
        }
    }
}

void platform::toggle_touchpads()
{
    auto changed{false};
    touchpads_enabled = !touchpads_enabled;

    for (auto& pointer : pointers) {
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

void platform::enable_touchpads()
{
    if (touchpads_enabled) {
        return;
    }
    toggle_touchpads();
}

void platform::disable_touchpads()
{
    if (!touchpads_enabled) {
        return;
    }
    toggle_touchpads();
}

void platform::start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                                  QByteArray const& cursorName)
{
    if (!redirect) {
        callback(nullptr);
        return;
    }
    redirect->startInteractiveWindowSelection(callback, cursorName);
}

void platform::start_interactive_position_selection(std::function<void(QPoint const&)> callback)
{
    if (!redirect) {
        callback(QPoint(-1, -1));
        return;
    }
    redirect->startInteractivePositionSelection(callback);
}

void platform::turn_outputs_on()
{
    base::wayland::turn_outputs_on(base, dpms_filter);
}

void platform::warp_pointer(QPointF const& pos, uint32_t time)
{
    if (pointers.empty()) {
        return;
    }

    redirect->get_pointer()->processMotion(pos, time, pointers.front());
}

void platform::setup_touchpad_shortcuts()
{
    auto toggle_action = new QAction(this);
    auto on_action = new QAction(this);
    auto off_action = new QAction(this);

    constexpr auto const component{"kcm_touchpad"};

    toggle_action->setObjectName(QStringLiteral("Toggle Touchpad"));
    toggle_action->setProperty("componentName", component);
    on_action->setObjectName(QStringLiteral("Enable Touchpad"));
    on_action->setProperty("componentName", component);
    off_action->setObjectName(QStringLiteral("Disable Touchpad"));
    off_action->setProperty("componentName", component);

    KGlobalAccel::self()->setDefaultShortcut(toggle_action,
                                             QList<QKeySequence>{Qt::Key_TouchpadToggle});
    KGlobalAccel::self()->setShortcut(toggle_action, QList<QKeySequence>{Qt::Key_TouchpadToggle});
    KGlobalAccel::self()->setDefaultShortcut(on_action, QList<QKeySequence>{Qt::Key_TouchpadOn});
    KGlobalAccel::self()->setShortcut(on_action, QList<QKeySequence>{Qt::Key_TouchpadOn});
    KGlobalAccel::self()->setDefaultShortcut(off_action, QList<QKeySequence>{Qt::Key_TouchpadOff});
    KGlobalAccel::self()->setShortcut(off_action, QList<QKeySequence>{Qt::Key_TouchpadOff});

    registerShortcut(Qt::Key_TouchpadToggle, toggle_action);
    registerShortcut(Qt::Key_TouchpadOn, on_action);
    registerShortcut(Qt::Key_TouchpadOff, off_action);

    QObject::connect(toggle_action, &QAction::triggered, this, &platform::toggle_touchpads);
    QObject::connect(on_action, &QAction::triggered, this, &platform::enable_touchpads);
    QObject::connect(off_action, &QAction::triggered, this, &platform::disable_touchpads);
}

void add_dbus(input::platform* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager>(platform);
}

}
