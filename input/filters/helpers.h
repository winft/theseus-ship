/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QWheelEvent>

namespace KWin
{
class Toplevel;

namespace input
{

enum class MouseAction {
    ModifierOnly,
    ModifierAndWindow,
};

std::pair<bool, bool> perform_client_mouse_action(QMouseEvent* event,
                                                  Toplevel* client,
                                                  MouseAction action = MouseAction::ModifierOnly);
std::pair<bool, bool> perform_client_wheel_action(QWheelEvent* event,
                                                  Toplevel* c,
                                                  MouseAction action = MouseAction::ModifierOnly);

}
}
