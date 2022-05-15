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

static win::wayland::space& wlspace()
{
    return static_cast<win::wayland::space&>(*workspace());
}

touch::touch(Wrapland::Server::FakeInputDevice* device, input::platform* platform)
    : input::touch(platform)
    , device{device}
{
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchDownRequested,
                     this,
                     [this](auto id, auto const& pos) {
                         // TODO: Fix time
                         this->platform->redirect->touch()->process_down(
                             {static_cast<int32_t>(id), pos, nullptr, 0});
                         wlspace().kde_idle->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchMotionRequested,
                     this,
                     [this](auto id, auto const& pos) {
                         // TODO: Fix time
                         this->platform->redirect->touch()->process_motion(
                             {static_cast<int32_t>(id), pos, nullptr, 0});
                         wlspace().kde_idle->simulateUserActivity();
                     });
    QObject::connect(
        device, &Wrapland::Server::FakeInputDevice::touchUpRequested, this, [this](auto id) {
            // TODO: Fix time
            this->platform->redirect->touch()->process_up({static_cast<int32_t>(id), {nullptr, 0}});
            wlspace().kde_idle->simulateUserActivity();
        });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchCancelRequested,
                     this,
                     [this]() { this->platform->redirect->touch()->cancel(); });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::touchFrameRequested,
                     this,
                     [this]() { this->platform->redirect->touch()->frame(); });
}

}
