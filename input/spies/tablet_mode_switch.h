/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_spy.h"

#include <QObject>

namespace KWin::input
{

namespace dbus
{
class tablet_mode_manager;
}

class tablet_mode_switch_spy : public QObject, public input::event_spy
{
public:
    explicit tablet_mode_switch_spy(dbus::tablet_mode_manager* manager);

    void switch_toggle(switch_toggle_event const& event) override;

private:
    dbus::tablet_mode_manager* const manager;
};

}
