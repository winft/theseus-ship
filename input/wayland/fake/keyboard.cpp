/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "input/keyboard_redirect.h"
#include "input/redirect.h"
#include "main.h"
#include "win/wayland/space.h"

#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/kde_idle.h>

namespace KWin::input::wayland::fake
{

using wayland_space = win::wayland::space<base::wayland::platform>;

static wayland_space& wlspace(win::space& space)
{
    return static_cast<wayland_space&>(space);
}

keyboard::keyboard(Wrapland::Server::FakeInputDevice* device, input::platform* platform)
    : input::keyboard(platform->xkb.context, platform->xkb.compose_table)
    , platform{platform}
    , device{device}
{
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::keyboardKeyPressRequested,
        this,
        [this](auto button) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_keyboard()->process_key({button, key_state::pressed, false, this, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::keyboardKeyReleaseRequested,
        this,
        [this](auto button) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_keyboard()->process_key({button, key_state::released, false, this, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
}

}
