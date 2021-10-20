/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

// TODO(romangg): should only be included when KWIN_BUILD_TABBOX is defined.
#include "input/filters/tabbox.h"

#include "input/filters/decoration_event.h"
#include "input/filters/drag_and_drop.h"
#include "input/filters/effects.h"
#include "input/filters/fake_tablet.h"
#include "input/filters/forward.h"
#include "input/filters/global_shortcut.h"
#include "input/filters/internal_window.h"
#include "input/filters/lock_screen.h"
#include "input/filters/move_resize.h"
#include "input/filters/popup.h"
#include "input/filters/screen_edge.h"
#include "input/filters/terminate_server.h"
#include "input/filters/virtual_terminal.h"
#include "input/filters/window_action.h"
#include "input/filters/window_selector.h"
#include "input/spies/touch_hide_cursor.h"

#include "main.h"
#include "seat/session.h"
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

void redirect::setupInputFilters()
{
    auto const has_global_shortcuts = waylandServer()->hasGlobalShortcutSupport();

    if (kwinApp()->session->hasSessionControl() && has_global_shortcuts) {
        installInputEventFilter(new virtual_terminal_filter);
    }

    installInputEventSpy(new touch_hide_cursor_spy);
    if (has_global_shortcuts) {
        installInputEventFilter(new terminate_server_filter);
    }
    installInputEventFilter(new drag_and_drop_filter);
    installInputEventFilter(new lock_screen_filter);
    installInputEventFilter(new popup_filter);

    m_windowSelector = new window_selector_filter;
    installInputEventFilter(m_windowSelector);

    if (has_global_shortcuts) {
        installInputEventFilter(new screen_edge_filter);
    }
    installInputEventFilter(new effects_filter);
    installInputEventFilter(new move_resize_filter);

#ifdef KWIN_BUILD_TABBOX
    installInputEventFilter(new tabbox_filter);
#endif

    if (has_global_shortcuts) {
        installInputEventFilter(new global_shortcut_filter);
    }

    installInputEventFilter(new decoration_event_filter);
    installInputEventFilter(new internal_window_filter);

    installInputEventFilter(new window_action_filter);
    installInputEventFilter(new forward_filter);
    installInputEventFilter(new fake_tablet_filter);
}

}
