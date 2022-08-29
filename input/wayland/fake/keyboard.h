/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard.h"

#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/kde_idle.h>

namespace KWin::input::wayland::fake
{

template<typename Platform>
class keyboard : public input::keyboard
{
public:
    keyboard(Wrapland::Server::FakeInputDevice* device, Platform* platform)
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
                redirect->keyboard->process_key({button, key_state::pressed, false, this, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::keyboardKeyReleaseRequested,
            this,
            [this](auto button) {
                auto redirect = this->platform->redirect;
                // TODO: Fix time
                redirect->keyboard->process_key({button, key_state::released, false, this, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
    }

    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    ~keyboard() override = default;

private:
    Platform* platform;
    Wrapland::Server::FakeInputDevice* device;
};

}
