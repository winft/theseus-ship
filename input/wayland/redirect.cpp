/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "main.h"
#include "wayland_server.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/fake_input.h>

namespace KWin::input::wayland
{

redirect::redirect()
    : input::redirect(new keyboard_redirect(this),
                      new pointer_redirect,
                      new tablet_redirect,
                      new touch_redirect)
{
}

redirect::~redirect() = default;

void redirect::setupWorkspace()
{
    fake_input = waylandServer()->display()->createFakeInput();

    QObject::connect(
        fake_input.get(), &Wrapland::Server::FakeInput::deviceCreated, this, [this](auto device) {
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::authenticationRequested,
                             this,
                             [this, device](auto const& /*application*/, auto const& /*reason*/) {
                                 // TODO: make secure
                                 device->setAuthentication(true);
                             });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::pointerMotionRequested,
                             this,
                             [this](auto const& delta) {
                                 // TODO: Fix time
                                 m_pointer->processMotion(
                                     globalPointer() + QPointF(delta.width(), delta.height()), 0);
                                 waylandServer()->simulateUserActivity();
                             });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::pointerMotionAbsoluteRequested,
                             this,
                             [this](auto const& pos) {
                                 // TODO: Fix time
                                 m_pointer->processMotion(pos, 0);
                                 waylandServer()->simulateUserActivity();
                             });
            QObject::connect(
                device,
                &Wrapland::Server::FakeInputDevice::pointerButtonPressRequested,
                this,
                [this](auto button) {
                    // TODO: Fix time
                    m_pointer->process_button({button, button_state::pressed, {nullptr, 0}});
                    waylandServer()->simulateUserActivity();
                });
            QObject::connect(
                device,
                &Wrapland::Server::FakeInputDevice::pointerButtonReleaseRequested,
                this,
                [this](auto button) {
                    // TODO: Fix time
                    m_pointer->process_button({button, button_state::released, {nullptr, 0}});
                    waylandServer()->simulateUserActivity();
                });
            QObject::connect(
                device,
                &Wrapland::Server::FakeInputDevice::pointerAxisRequested,
                this,
                [this](auto orientation, auto delta) {
                    // TODO: Fix time
                    auto axis = (orientation == Qt::Horizontal) ? axis_orientation::horizontal
                                                                : axis_orientation::vertical;
                    // TODO: Fix time
                    m_pointer->process_axis({axis_source::unknown, axis, delta, 0, nullptr, 0});
                    waylandServer()->simulateUserActivity();
                });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::touchDownRequested,
                             this,
                             [this](auto id, auto const& pos) {
                                 // TODO: Fix time
                                 m_touch->processDown(id, pos, 0);
                                 waylandServer()->simulateUserActivity();
                             });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::touchMotionRequested,
                             this,
                             [this](auto id, auto const& pos) {
                                 // TODO: Fix time
                                 m_touch->processMotion(id, pos, 0);
                                 waylandServer()->simulateUserActivity();
                             });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::touchUpRequested,
                             this,
                             [this](auto id) {
                                 // TODO: Fix time
                                 m_touch->processUp(id, 0);
                                 waylandServer()->simulateUserActivity();
                             });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::touchCancelRequested,
                             this,
                             [this]() { m_touch->cancel(); });
            QObject::connect(device,
                             &Wrapland::Server::FakeInputDevice::touchFrameRequested,
                             this,
                             [this]() { m_touch->frame(); });
            QObject::connect(
                device,
                &Wrapland::Server::FakeInputDevice::keyboardKeyPressRequested,
                this,
                [this](auto button) {
                    // TODO: Fix time
                    m_keyboard->process_key({button, button_state::pressed, false, nullptr, 0});
                    waylandServer()->simulateUserActivity();
                });
            QObject::connect(
                device,
                &Wrapland::Server::FakeInputDevice::keyboardKeyReleaseRequested,
                this,
                [this](auto button) {
                    // TODO: Fix time
                    m_keyboard->process_key({button, button_state::released, false, nullptr, 0});
                    waylandServer()->simulateUserActivity();
                });
        });

    m_keyboard->init();
    m_pointer->init();
    m_touch->init();
    m_tablet->init();

    setupInputFilters();
}

}
