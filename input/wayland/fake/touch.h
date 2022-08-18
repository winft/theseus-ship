/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/touch.h"

#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/kde_idle.h>

namespace KWin::input::wayland::fake
{

template<typename Platform>
class touch : public input::touch
{
public:
    touch(Wrapland::Server::FakeInputDevice* device, Platform* platform)
        : platform{platform}
        , device{device}
    {
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::touchDownRequested,
            this->qobject.get(),
            [this](auto id, auto const& pos) {
                auto redirect = this->platform->redirect;
                // TODO: Fix time
                redirect->touch->process_down({static_cast<int32_t>(id), pos, nullptr, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::touchMotionRequested,
            this->qobject.get(),
            [this](auto id, auto const& pos) {
                auto redirect = this->platform->redirect;
                // TODO: Fix time
                redirect->touch->process_motion({static_cast<int32_t>(id), pos, nullptr, 0});
                redirect->platform.base.space->kde_idle->simulateUserActivity();
            });
        QObject::connect(device,
                         &Wrapland::Server::FakeInputDevice::touchUpRequested,
                         this->qobject.get(),
                         [this](auto id) {
                             auto redirect = this->platform->redirect;
                             // TODO: Fix time
                             redirect->touch->process_up({static_cast<int32_t>(id), {nullptr, 0}});
                             redirect->platform.base.space->kde_idle->simulateUserActivity();
                         });
        QObject::connect(device,
                         &Wrapland::Server::FakeInputDevice::touchCancelRequested,
                         this->qobject.get(),
                         [this]() { this->platform->redirect->touch->cancel(); });
        QObject::connect(device,
                         &Wrapland::Server::FakeInputDevice::touchFrameRequested,
                         this->qobject.get(),
                         [this]() { this->platform->redirect->touch->frame(); });
    }

    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;

private:
    Platform* platform;
    Wrapland::Server::FakeInputDevice* device;
};

}
