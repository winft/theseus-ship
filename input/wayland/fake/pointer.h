/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/pointer.h"

namespace Wrapland::Server
{
class FakeInputDevice;
}

namespace KWin::input
{
class platform;

namespace wayland::fake
{

class pointer : public input::pointer
{
    Q_OBJECT
public:
    pointer(Wrapland::Server::FakeInputDevice* device, input::platform* platform);
    pointer(pointer const&) = delete;
    pointer& operator=(pointer const&) = delete;
    ~pointer() override = default;

private:
    input::platform* platform;
    Wrapland::Server::FakeInputDevice* device;
};

}
}
