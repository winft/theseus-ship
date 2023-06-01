/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/touch.h"

#include <Wrapland/Server/fake_input.h>

namespace KWin::input::wayland::fake
{

template<typename Redirect>
class touch : public input::touch
{
public:
    touch(Wrapland::Server::FakeInputDevice* device, Redirect& redirect)
        : redirect{redirect}
        , device{device}
    {
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::touchDownRequested,
            this->qobject.get(),
            [this](auto id, auto const& pos) {
                // TODO: Fix time
                this->redirect.touch->process_down({static_cast<int32_t>(id), pos, {nullptr, 0}});
            });
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::touchMotionRequested,
            this->qobject.get(),
            [this](auto id, auto const& pos) {
                // TODO: Fix time
                this->redirect.touch->process_motion({static_cast<int32_t>(id), pos, {nullptr, 0}});
            });
        QObject::connect(
            device,
            &Wrapland::Server::FakeInputDevice::touchUpRequested,
            this->qobject.get(),
            [this](auto id) {
                // TODO: Fix time
                this->redirect.touch->process_up({static_cast<int32_t>(id), {nullptr, 0}});
            });
        QObject::connect(device,
                         &Wrapland::Server::FakeInputDevice::touchCancelRequested,
                         this->qobject.get(),
                         [this]() { this->redirect.touch->cancel(); });
        QObject::connect(device,
                         &Wrapland::Server::FakeInputDevice::touchFrameRequested,
                         this->qobject.get(),
                         [this]() { this->redirect.touch->frame(); });
    }

    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;

private:
    Redirect& redirect;
    Wrapland::Server::FakeInputDevice* device;
};

}
