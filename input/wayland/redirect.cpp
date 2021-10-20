/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "input/keyboard.h"
#include "input/platform.h"
#include "input/pointer.h"
#include "input/switch.h"
#include "input/touch.h"

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
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin::input::wayland
{

redirect::redirect()
    : input::redirect(new keyboard_redirect(this),
                      new pointer_redirect,
                      new tablet_redirect,
                      new touch_redirect)
{
    reconfigure();
}

redirect::~redirect() = default;

static Wrapland::Server::Seat* find_seat()
{
    return waylandServer()->seat();
}

void redirect::set_platform(input::platform* platform)
{
    this->platform = platform;
    platform->config = kwinApp()->inputConfig();

    QObject::connect(platform, &platform::pointer_added, this, [this](auto pointer) {
        auto pointer_red = m_pointer.get();
        QObject::connect(pointer,
                         &pointer::button_changed,
                         pointer_red,
                         &input::pointer_redirect::process_button);
        QObject::connect(
            pointer, &pointer::motion, pointer_red, &input::pointer_redirect::process_motion);
        QObject::connect(pointer,
                         &pointer::motion_absolute,
                         pointer_red,
                         &input::pointer_redirect::process_motion_absolute);
        QObject::connect(
            pointer, &pointer::axis_changed, pointer_red, &input::pointer_redirect::process_axis);

        QObject::connect(pointer,
                         &pointer::pinch_begin,
                         pointer_red,
                         &input::pointer_redirect::process_pinch_begin);
        QObject::connect(pointer,
                         &pointer::pinch_update,
                         pointer_red,
                         &input::pointer_redirect::process_pinch_update);
        QObject::connect(
            pointer, &pointer::pinch_end, pointer_red, &input::pointer_redirect::process_pinch_end);

        QObject::connect(pointer,
                         &pointer::swipe_begin,
                         pointer_red,
                         &input::pointer_redirect::process_swipe_begin);
        QObject::connect(pointer,
                         &pointer::swipe_update,
                         pointer_red,
                         &input::pointer_redirect::process_swipe_update);
        QObject::connect(
            pointer, &pointer::swipe_end, pointer_red, &input::pointer_redirect::process_swipe_end);

        if (auto seat = find_seat()) {
            seat->setHasPointer(true);
        }
    });

    QObject::connect(platform, &platform::pointer_removed, this, [this, platform]() {
        if (auto seat = find_seat(); seat && platform->pointers.empty()) {
            seat->setHasPointer(false);
        }
    });

    QObject::connect(platform, &platform::switch_added, this, [this](auto switch_device) {
        QObject::connect(switch_device, &switch_device::toggle, this, [this](auto const& event) {
            if (event.type == switch_type::tablet_mode) {
                Q_EMIT hasTabletModeSwitchChanged(event.state == switch_state::on);
            }
        });
    });

    QObject::connect(platform, &platform::touch_added, this, [this](auto touch) {
        auto touch_red = m_touch.get();
        QObject::connect(touch, &touch::down, touch_red, &input::touch_redirect::process_down);
        QObject::connect(touch, &touch::up, touch_red, &input::touch_redirect::process_up);
        QObject::connect(touch, &touch::motion, touch_red, &input::touch_redirect::process_motion);
        QObject::connect(touch, &touch::cancel, touch_red, &input::touch_redirect::cancel);
#if HAVE_WLR_TOUCH_FRAME
        QObject::connect(touch, &touch::frame, touch_red, &input::touch_redirect::frame);
#endif

        if (auto seat = find_seat()) {
            seat->setHasTouch(true);
        }
    });

    QObject::connect(platform, &platform::touch_removed, this, [this, platform]() {
        if (auto seat = find_seat(); seat && platform->touchs.empty()) {
            seat->setHasTouch(false);
        }
    });

    QObject::connect(platform, &platform::keyboard_added, this, [this](auto keyboard) {
        auto keyboard_red = m_keyboard.get();
        QObject::connect(
            keyboard, &keyboard::key_changed, keyboard_red, &input::keyboard_redirect::process_key);
        QObject::connect(keyboard,
                         &keyboard::modifiers_changed,
                         keyboard_red,
                         &input::keyboard_redirect::process_modifiers);
        if (auto seat = find_seat(); seat && !seat->hasKeyboard()) {
            seat->setHasKeyboard(true);
            reconfigure();
        }
    });

    QObject::connect(platform, &platform::keyboard_removed, this, [this, platform]() {
        if (auto seat = find_seat(); seat && platform->keyboards.empty()) {
            seat->setHasKeyboard(false);
        }
    });

    platform->update_keyboard_leds(m_keyboard->xkb()->leds());
    waylandServer()->updateKeyState(m_keyboard->xkb()->leds());
    QObject::connect(m_keyboard.get(),
                     &keyboard_redirect::ledsChanged,
                     waylandServer(),
                     &WaylandServer::updateKeyState);
    QObject::connect(m_keyboard.get(),
                     &keyboard_redirect::ledsChanged,
                     platform,
                     &platform::update_keyboard_leds);

    reconfigure();
    QObject::connect(m_inputConfigWatcher.data(),
                     &KConfigWatcher::configChanged,
                     this,
                     [this](auto const& group) {
                         if (group.name() == QLatin1String("Keyboard")) {
                             reconfigure();
                         }
                     });

    setupTouchpadShortcuts();
}

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

void redirect::reconfigure()
{
    auto input_config = m_inputConfigWatcher->config();
    auto const group = input_config->group(QStringLiteral("Keyboard"));

    auto delay = group.readEntry("RepeatDelay", 660);
    auto rate = group.readEntry("RepeatRate", 25);
    auto const repeat = group.readEntry("KeyRepeat", "repeat");

    // When the clients will repeat the character or turn repeat key events into an accent character
    // selection, we want to tell the clients that we are indeed repeating keys.
    auto enabled = repeat == QLatin1String("accent") || repeat == QLatin1String("repeat");

    if (waylandServer()->seat()->hasKeyboard()) {
        waylandServer()->seat()->keyboards().set_repeat_info(enabled ? rate : 0, delay);
    }
}

}
