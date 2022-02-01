/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "input/keyboard_redirect.h"
#include "input/redirect.h"
#include "main.h"
#include "wayland_server.h"

#include <Wrapland/Server/fake_input.h>

namespace KWin::input::wayland::fake
{

keyboard::keyboard(Wrapland::Server::FakeInputDevice* device, input::platform* platform)
    : input::keyboard(platform)
    , device{device}
{
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::keyboardKeyPressRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         this->platform->redirect->keyboard()->process_key(
                             {button, key_state::pressed, false, this, 0});
                         waylandServer()->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::keyboardKeyReleaseRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         this->platform->redirect->keyboard()->process_key(
                             {button, key_state::released, false, this, 0});
                         waylandServer()->simulateUserActivity();
                     });
}

}
