/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

#include "input/platform.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
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

pointer::pointer(Wrapland::Server::FakeInputDevice* device, input::platform* platform)
    : input::pointer(platform)
    , device{device}
{
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::pointerMotionRequested,
        this,
        [this](auto const& delta) {
            // TODO: Fix time
            auto redirect = this->platform->redirect;
            redirect->pointer()->process_motion_absolute(
                {redirect->globalPointer() + QPointF(delta.width(), delta.height()), {this, 0}});
            wlspace().kde_idle->simulateUserActivity();
        });
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::pointerMotionAbsoluteRequested,
        this,
        [this](auto const& pos) {
            // TODO: Fix time
            this->platform->redirect->pointer()->process_motion_absolute({pos, this, 0});
            wlspace().kde_idle->simulateUserActivity();
        });

    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerButtonPressRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         this->platform->redirect->pointer()->process_button(
                             {button, button_state::pressed, this, 0});
                         wlspace().kde_idle->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerButtonReleaseRequested,
                     this,
                     [this](auto button) {
                         // TODO: Fix time
                         this->platform->redirect->pointer()->process_button(
                             {button, button_state::released, this, 0});
                         wlspace().kde_idle->simulateUserActivity();
                     });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerAxisRequested,
                     this,
                     [this](auto orientation, auto delta) {
                         // TODO: Fix time
                         auto axis = (orientation == Qt::Horizontal) ? axis_orientation::horizontal
                                                                     : axis_orientation::vertical;
                         // TODO: Fix time
                         this->platform->redirect->pointer()->process_axis(
                             {axis_source::unknown, axis, delta, 0, this, 0});
                         wlspace().kde_idle->simulateUserActivity();
                     });
}

}
