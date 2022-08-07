/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/touch.h"

namespace Wrapland::Server
{
class FakeInputDevice;
}

namespace KWin::input
{
class platform;

namespace wayland::fake
{

class touch : public input::touch
{
    Q_OBJECT
public:
    touch(Wrapland::Server::FakeInputDevice* device, input::platform* platform);
    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;
    ~touch() override = default;

private:
    input::platform* platform;
    Wrapland::Server::FakeInputDevice* device;
};

}
}
