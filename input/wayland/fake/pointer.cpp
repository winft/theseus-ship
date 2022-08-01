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

using wayland_space = win::wayland::space<base::wayland::platform>;

static wayland_space& wlspace(win::space& space)
{
    return static_cast<wayland_space&>(space);
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
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_pointer()->process_motion_absolute(
                {redirect->globalPointer() + QPointF(delta.width(), delta.height()), {this, 0}});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::pointerMotionAbsoluteRequested,
                     this,
                     [this](auto const& pos) {
                         auto redirect = this->platform->redirect;
                         // TODO: Fix time
                         redirect->get_pointer()->process_motion_absolute({pos, this, 0});
                         wlspace(redirect->space).kde_idle->simulateUserActivity();
                     });

    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::pointerButtonPressRequested,
        this,
        [this](auto button) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_pointer()->process_button({button, button_state::pressed, this, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::pointerButtonReleaseRequested,
        this,
        [this](auto button) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            redirect->get_pointer()->process_button({button, button_state::released, this, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
    QObject::connect(
        device,
        &Wrapland::Server::FakeInputDevice::pointerAxisRequested,
        this,
        [this](auto orientation, auto delta) {
            auto redirect = this->platform->redirect;
            // TODO: Fix time
            auto axis = (orientation == Qt::Horizontal) ? axis_orientation::horizontal
                                                        : axis_orientation::vertical;
            // TODO: Fix time
            redirect->get_pointer()->process_axis({axis_source::unknown, axis, delta, 0, this, 0});
            wlspace(redirect->space).kde_idle->simulateUserActivity();
        });
}

}
