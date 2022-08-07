/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "input/platform.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
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

touch::touch(Wrapland::Server::FakeInputDevice* device, input::platform* platform)
    : platform{platform}
    , device{device}
{
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::touchDownRequested,
        this,
        [this](auto id, auto const& pos) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_touch()->process_down({static_cast<int32_t>(id), pos, nullptr, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::touchMotionRequested,
        this,
        [this](auto id, auto const& pos) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_touch()->process_motion({static_cast<int32_t>(id), pos, nullptr, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(
        device, &Wrapland::Server::FakeInputDevice::touchUpRequested, this, [this](auto id) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_touch()->process_up({static_cast<int32_t>(id), {nullptr, 0}});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchCancelRequested,
                     this,
                     [this]() { this->platform->redirect->get_touch()->cancel(); });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchFrameRequested,
                     this,
                     [this]() { this->platform->redirect->get_touch()->frame(); });
}

}
