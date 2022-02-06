/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tabbox.h"

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "main.h"
#include "tabbox/tabbox.h"
#include "workspace.h"

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

bool tabbox_filter::button(button_event const& event)
{
    if (!TabBox::TabBox::self() || !TabBox::TabBox::self()->isGrabbed()) {
        return false;
    }

    auto qt_event = button_to_qt_event(event);
    return TabBox::TabBox::self()->handleMouseEvent(&qt_event);
}

bool tabbox_filter::motion(motion_event const& event)
{
    if (!TabBox::TabBox::self() || !TabBox::TabBox::self()->isGrabbed()) {
        return false;
    }

    auto qt_event = motion_to_qt_event(event);
    return TabBox::TabBox::self()->handleMouseEvent(&qt_event);
}

bool tabbox_filter::key(key_event const& event)
{
    if (!TabBox::TabBox::self() || !TabBox::TabBox::self()->isGrabbed()) {
        return false;
    }

    auto seat = waylandServer()->seat();
    seat->setFocusedKeyboardSurface(nullptr);
    kwinApp()->input->redirect->pointer()->setEnableConstraints(false);

    // pass the key event to the seat, so that it has a proper model of the currently hold keys
    // this is important for combinations like alt+shift to ensure that shift is not considered
    // pressed
    pass_to_wayland_server(event);

    if (event.state == key_state::pressed) {
        auto mods = xkb::get_active_keyboard_modifiers(kwinApp()->input);
        TabBox::TabBox::self()->keyPress(mods
                                         | key_to_qt_key(event.keycode, event.base.dev->xkb.get()));
    } else if (xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(kwinApp()->input)
               == Qt::NoModifier) {
        TabBox::TabBox::self()->modifiersReleased();
    }
    return true;
}

bool tabbox_filter::key_repeat(key_event const& event)
{
    if (!TabBox::TabBox::self() || !TabBox::TabBox::self()->isGrabbed()) {
        return false;
    }

    auto mods = xkb::get_active_keyboard_modifiers(kwinApp()->input);
    TabBox::TabBox::self()->keyPress(mods
                                     | key_to_qt_key(event.keycode, event.base.dev->xkb.get()));
    return true;
}

bool tabbox_filter::axis(axis_event const& event)
{
    if (!TabBox::TabBox::self() || !TabBox::TabBox::self()->isGrabbed()) {
        return false;
    }

    auto qt_event = axis_to_qt_event(event);
    return TabBox::TabBox::self()->handleWheelEvent(&qt_event);
}

}
