/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/pointer.h"

#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/kde_idle.h>

namespace KWin::input::wayland::fake
{

template<typename Platform>
class pointer : public input::pointer
{
public:
    pointer(Wrapland::Server::FakeInputDevice* device, Platform* platform)
        : platform{platform}
        , device{device}
    {
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::pointerMotionRequested,
            this,
            [this](auto const& delta) {
                auto redirect = this->platform->redirect;
                // TODO: Fix time
                redirect->pointer->process_motion_absolute(
                    {redirect->globalPointer() + QPointF(delta.width(), delta.height()),
                     {this, 0}});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
        QObject::connect(device,
                         &Wrapland::Server::FakeInputDevice::pointerMotionAbsoluteRequested,
                         this,
                         [this](auto const& pos) {
                             auto redirect = this->platform->redirect;
                             // TODO: Fix time
                             redirect->pointer->process_motion_absolute({pos, this, 0});
                             redirect->platform.base.space->kde_idle->simulateUserActivity();
                         });

        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::pointerButtonPressRequested,
            this,
            [this](auto button) {
                auto redirect = this->platform->redirect;
                // TODO: Fix time
                redirect->pointer->process_button({button, button_state::pressed, this, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::pointerButtonReleaseRequested,
            this,
            [this](auto button) {
                auto redirect = this->platform->redirect;
                // TODO: Fix time
                redirect->pointer->process_button({button, button_state::released, this, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
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
                redirect->pointer->process_axis({axis_source::unknown, axis, delta, 0, this, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
    }

    pointer(pointer const&) = delete;
    pointer& operator=(pointer const&) = delete;
    ~pointer() override = default;

private:
    Platform* platform;
    Wrapland::Server::FakeInputDevice* device;
};

}
