/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard.h"

namespace Wrapland::Server
{
class FakeInputDevice;
}

namespace KWin::input
{
class platform;

namespace wayland::fake
{

class keyboard : public input::keyboard
{
    Q_OBJECT
public:
    keyboard(Wrapland::Server::FakeInputDevice* device, input::platform* platform);
    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    ~keyboard() override = default;

private:
    input::platform* platform;
    Wrapland::Server::FakeInputDevice* device;
};

}
}
