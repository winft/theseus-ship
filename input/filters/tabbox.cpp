/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tabbox.h"

#include "../pointer_redirect.h"
#include "input/event.h"
#include "input/qt_event.h"
#include "main.h"
#include "tabbox/tabbox.h"
#include "wayland_server.h"
#include "workspace.h"

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

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

bool tabbox_filter::keyEvent(QKeyEvent* event)
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
    passToWaylandServer(event);

    if (event->type() == QEvent::KeyPress) {
        TabBox::TabBox::self()->keyPress(event->modifiers() | event->key());
    } else if (static_cast<input::KeyEvent*>(event)->modifiersRelevantForGlobalShortcuts()
               == Qt::NoModifier) {
        TabBox::TabBox::self()->modifiersReleased();
    }
    return true;
}

bool tabbox_filter::key_repeat(QKeyEvent* event)
{
    if (!TabBox::TabBox::self() || !TabBox::TabBox::self()->isGrabbed()) {
        return false;
    }
    TabBox::TabBox::self()->keyPress(event->modifiers() | event->key());
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
