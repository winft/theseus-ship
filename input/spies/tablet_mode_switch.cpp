/*
    SPDX-FileCopyrightText: 2018 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tablet_mode_switch.h"

#include "input/dbus/tablet_mode_manager.h"
#include "input/event.h"
#include "input/switch.h"

namespace KWin::input
{

tablet_mode_switch_spy::tablet_mode_switch_spy(dbus::tablet_mode_manager* parent)
    : QObject(parent)
    , m_parent(parent)
{
}

void tablet_mode_switch_spy::switchEvent(input::SwitchEvent* event)
{
    if (auto& ctrl = event->device()->control; !ctrl || !ctrl->is_tablet_mode_switch()) {
        return;
    }

    switch (event->state()) {
    case input::SwitchEvent::State::Off:
        m_parent->setIsTablet(false);
        break;
    case input::SwitchEvent::State::On:
        m_parent->setIsTablet(true);
        break;
    default:
        Q_UNREACHABLE();
    }
}

}
