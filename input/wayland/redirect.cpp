/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "cursor.h"
#include "device_redirect.h"
#include "keyboard_redirect.h"
#include "platform.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "fake/keyboard.h"
#include "fake/pointer.h"
#include "fake/touch.h"

#include "input/event_filter.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/switch.h"
#include "input/touch.h"
#include "input/xkb/keyboard.h"

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

#include "base/seat/session.h"
#include "base/wayland/server.h"
#include "main.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>
#include <Wrapland/Server/virtual_keyboard_v1.h>

namespace KWin::input::wayland
{

static Wrapland::Server::Seat* find_seat()
{
    return waylandServer()->seat();
}

wayland::platform& wl_plat(input::platform& platform)
{
    return static_cast<wayland::platform&>(platform);
}

redirect::redirect(input::platform& platform, win::space& space)
    : input::redirect(platform, space)
    , config_watcher{KConfigWatcher::create(kwinApp()->inputConfig())}
{
    setup_workspace();
}

template<typename Dev>
void unset_focus(Dev&& dev)
{
    dev->focusUpdate(dev->focus.window, nullptr);
}

void redirect::setup_devices()
{
    for (auto pointer : platform.pointers) {
        handle_pointer_added(pointer);
    }
    QObject::connect(platform.qobject.get(),
                     &platform_qobject::pointer_added,
                     qobject.get(),
                     [this](auto pointer) { handle_pointer_added(pointer); });
    QObject::connect(
        platform.qobject.get(), &platform_qobject::pointer_removed, qobject.get(), [this]() {
            if (platform.pointers.empty()) {
                auto seat = find_seat();
                unset_focus(pointer.get());
                seat->setHasPointer(false);
            }
        });

    for (auto keyboard : platform.keyboards) {
        handle_keyboard_added(keyboard);
    }
    QObject::connect(platform.qobject.get(),
                     &platform_qobject::keyboard_added,
                     qobject.get(),
                     [this](auto keys) { handle_keyboard_added(keys); });
    QObject::connect(
        platform.qobject.get(), &platform_qobject::keyboard_removed, qobject.get(), [this]() {
            if (platform.keyboards.empty()) {
                auto seat = find_seat();
                seat->setFocusedKeyboardSurface(nullptr);
                seat->setHasKeyboard(false);
            }
        });

    for (auto touch : platform.touchs) {
        handle_touch_added(touch);
    }
    QObject::connect(platform.qobject.get(),
                     &platform_qobject::touch_added,
                     qobject.get(),
                     [this](auto touch) { handle_touch_added(touch); });
    QObject::connect(
        platform.qobject.get(), &platform_qobject::touch_removed, qobject.get(), [this]() {
            if (platform.touchs.empty()) {
                auto seat = find_seat();
                unset_focus(touch.get());
                seat->setHasTouch(false);
            }
        });

    for (auto switch_dev : platform.switches) {
        handle_switch_added(switch_dev);
    }
    QObject::connect(platform.qobject.get(),
                     &platform_qobject::switch_added,
                     qobject.get(),
                     [this](auto dev) { handle_switch_added(dev); });
}

redirect::~redirect()
{
    auto const filters = m_filters;
    for (auto filter : filters) {
        delete filter;
    }
}

input::keyboard_redirect* redirect::get_keyboard() const
{
    return keyboard.get();
}
input::pointer_redirect* redirect::get_pointer() const
{
    return pointer.get();
}
input::tablet_redirect* redirect::get_tablet() const
{
    return tablet.get();
}
input::touch_redirect* redirect::get_touch() const
{
    return touch.get();
}

void redirect::setup_workspace()
{
    reconfigure();
    QObject::connect(config_watcher.data(),
                     &KConfigWatcher::configChanged,
                     qobject.get(),
                     [this](auto const& group) {
                         if (group.name() == QLatin1String("Keyboard")) {
                             reconfigure();
                         }
                     });

    platform.cursor = std::make_unique<wayland::cursor>(&wl_plat(platform));

    pointer = std::make_unique<wayland::pointer_redirect>(this);
    keyboard = std::make_unique<wayland::keyboard_redirect>(this);
    touch = std::make_unique<wayland::touch_redirect>(this);
    tablet = std::make_unique<wayland::tablet_redirect>(this);

    setup_devices();

    fake_input = waylandServer()->display->createFakeInput();
    QObject::connect(fake_input.get(),
                     &Wrapland::Server::FakeInput::deviceCreated,
                     qobject.get(),
                     [this](auto device) { handle_fake_input_device_added(device); });
    QObject::connect(fake_input.get(),
                     &Wrapland::Server::FakeInput::device_destroyed,
                     qobject.get(),
                     [this](auto device) { fake_devices.erase(device); });

    QObject::connect(wl_plat(platform).virtual_keyboard.get(),
                     &Wrapland::Server::virtual_keyboard_manager_v1::keyboard_created,
                     qobject.get(),
                     [this](auto device) { handle_virtual_keyboard_added(device); });

    keyboard->init();
    pointer->init();
    touch->init();
    tablet->init();

    setup_filters();
}

void redirect::setup_filters()
{
    auto const has_global_shortcuts = waylandServer()->has_global_shortcut_support();

    if (kwinApp()->session->hasSessionControl() && has_global_shortcuts) {
        m_filters.emplace_back(new virtual_terminal_filter<redirect>(*this));
    }

    installInputEventSpy(new touch_hide_cursor_spy(*this));
    if (has_global_shortcuts) {
        m_filters.emplace_back(new terminate_server_filter<redirect>(*this));
    }
    m_filters.emplace_back(new drag_and_drop_filter<redirect>(*this));
    m_filters.emplace_back(new lock_screen_filter<redirect>(*this));
    m_filters.emplace_back(new popup_filter<redirect>(*this));

    window_selector = new window_selector_filter<redirect>(*this);
    m_filters.push_back(window_selector);

    if (has_global_shortcuts) {
        m_filters.emplace_back(new screen_edge_filter(*this));
    }
    m_filters.emplace_back(new effects_filter(*this));
    m_filters.emplace_back(new move_resize_filter(*this));

#if KWIN_BUILD_TABBOX
    m_filters.emplace_back(new tabbox_filter(*this));
#endif

    if (has_global_shortcuts) {
        m_filters.emplace_back(new global_shortcut_filter(*this));
    }

    m_filters.emplace_back(new decoration_event_filter(*this));
    m_filters.emplace_back(new internal_window_filter(*this));

    m_filters.emplace_back(new window_action_filter(*this));
    m_filters_install_iterator = m_filters.insert(m_filters.cend(), new forward_filter(*this));
    m_filters.emplace_back(new fake_tablet_filter(*this));
}

void redirect::reconfigure()
{
    auto input_config = config_watcher->config();
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

bool redirect::has_tablet_mode_switch()
{
    return std::any_of(platform.switches.cbegin(), platform.switches.cend(), [](auto dev) {
        return dev->control->is_tablet_mode_switch();
    });
}

void redirect::startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                               QByteArray const& cursorName)
{
    if (window_selector->isActive()) {
        callback(nullptr);
        return;
    }
    window_selector->start(callback);
    pointer->setWindowSelectionCursor(cursorName);
}

void redirect::startInteractivePositionSelection(std::function<void(QPoint const&)> callback)
{
    if (window_selector->isActive()) {
        callback(QPoint(-1, -1));
        return;
    }
    window_selector->start(callback);
    pointer->setWindowSelectionCursor(QByteArray());
}

bool redirect::isSelectingWindow() const
{
    // TODO(romangg): This function is called before setup_filters is run (from setup_workspace).
    //                Can we ensure it's only called afterwards and remove the nullptr check?
    return window_selector && window_selector->isActive();
}

void redirect::append_filter(event_filter<redirect>* filter)
{
    Q_ASSERT(!contains(m_filters, filter));
    m_filters.insert(m_filters_install_iterator, filter);
}

void redirect::prependInputEventFilter(event_filter<redirect>* filter)
{
    Q_ASSERT(!contains(m_filters, filter));
    m_filters.insert(m_filters.begin(), filter);
}

void redirect::uninstallInputEventFilter(event_filter<redirect>* filter)
{
    remove_all(m_filters, filter);
}

void redirect::handle_pointer_added(input::pointer* pointer)
{
    auto pointer_red = this->pointer.get();

    QObject::connect(
        pointer, &pointer::button_changed, pointer_red, &input::pointer_redirect::process_button);

    QObject::connect(
        pointer, &pointer::motion, pointer_red, &input::pointer_redirect::process_motion);
    QObject::connect(pointer,
                     &pointer::motion_absolute,
                     pointer_red,
                     &input::pointer_redirect::process_motion_absolute);

    QObject::connect(
        pointer, &pointer::axis_changed, pointer_red, &input::pointer_redirect::process_axis);

    QObject::connect(
        pointer, &pointer::pinch_begin, pointer_red, &input::pointer_redirect::process_pinch_begin);
    QObject::connect(pointer,
                     &pointer::pinch_update,
                     pointer_red,
                     &input::pointer_redirect::process_pinch_update);
    QObject::connect(
        pointer, &pointer::pinch_end, pointer_red, &input::pointer_redirect::process_pinch_end);

    QObject::connect(
        pointer, &pointer::swipe_begin, pointer_red, &input::pointer_redirect::process_swipe_begin);
    QObject::connect(pointer,
                     &pointer::swipe_update,
                     pointer_red,
                     &input::pointer_redirect::process_swipe_update);
    QObject::connect(
        pointer, &pointer::swipe_end, pointer_red, &input::pointer_redirect::process_swipe_end);

    QObject::connect(
        pointer, &pointer::frame, pointer_red, &input::pointer_redirect::process_frame);

    auto seat = find_seat();
    if (!seat->hasPointer()) {
        seat->setHasPointer(true);
        device_redirect_update_focus(this->pointer.get());
    }
}

void redirect::handle_keyboard_added(input::keyboard* keyboard)
{
    auto keyboard_red = this->keyboard.get();

    QObject::connect(
        keyboard, &keyboard::key_changed, keyboard_red, &input::keyboard_redirect::process_key);
    QObject::connect(keyboard,
                     &keyboard::modifiers_changed,
                     keyboard_red,
                     &input::keyboard_redirect::process_modifiers);

    auto seat = find_seat();

    if (!seat->hasKeyboard()) {
        seat->setHasKeyboard(true);
        this->keyboard->update();
        reconfigure();
    }

    keyboard->xkb->forward_modifiers_impl = [seat](auto keymap, auto&& mods, auto layout) {
        seat->keyboards().set_keymap(keymap->cache);
        seat->keyboards().update_modifiers(mods.depressed, mods.latched, mods.locked, layout);
    };
    keyboard->xkb->update_from_default();

    wl_plat(platform).update_keyboard_leds(keyboard->xkb->leds);
    waylandServer()->update_key_state(keyboard->xkb->leds);

    QObject::connect(keyboard->xkb.get(),
                     &xkb::keyboard::leds_changed,
                     waylandServer(),
                     &base::wayland::server::update_key_state);
    QObject::connect(keyboard->xkb.get(),
                     &xkb::keyboard::leds_changed,
                     platform.qobject.get(),
                     [this](auto leds) { wl_plat(platform).update_keyboard_leds(leds); });
}

void redirect::handle_touch_added(input::touch* touch)
{
    auto touch_red = this->touch.get();

    QObject::connect(touch, &touch::down, touch_red, &input::touch_redirect::process_down);
    QObject::connect(touch, &touch::up, touch_red, &input::touch_redirect::process_up);
    QObject::connect(touch, &touch::motion, touch_red, &input::touch_redirect::process_motion);
    QObject::connect(touch, &touch::cancel, touch_red, &input::touch_redirect::cancel);
    QObject::connect(touch, &touch::frame, touch_red, &input::touch_redirect::frame);

    auto seat = find_seat();
    if (!seat->hasTouch()) {
        seat->setHasTouch(true);
        device_redirect_update_focus(this->touch.get());
    }
}

void redirect::handle_switch_added(input::switch_device* switch_device)
{
    QObject::connect(
        switch_device, &switch_device::toggle, qobject.get(), [this](auto const& event) {
            if (event.type == switch_type::tablet_mode) {
                Q_EMIT qobject->has_tablet_mode_switch_changed(event.state == switch_state::on);
            }
        });
}

void redirect::handle_fake_input_device_added(Wrapland::Server::FakeInputDevice* device)
{
    QObject::connect(device,
                     &Wrapland::Server::FakeInputDevice::authenticationRequested,
                     qobject.get(),
                     [device](auto const& /*application*/, auto const& /*reason*/) {
                         // TODO: make secure
                         device->setAuthentication(true);
                     });

    auto devices = fake_input_devices({std::make_unique<fake::pointer>(device, &platform),
                                       std::make_unique<fake::keyboard>(device, &platform),
                                       std::make_unique<fake::touch>(device, &platform)});

    Q_EMIT platform.qobject->pointer_added(devices.pointer.get());
    Q_EMIT platform.qobject->keyboard_added(devices.keyboard.get());
    Q_EMIT platform.qobject->touch_added(devices.touch.get());

    fake_devices.insert({device, std::move(devices)});
}

void redirect::handle_virtual_keyboard_added(
    Wrapland::Server::virtual_keyboard_v1* virtual_keyboard)
{
    namespace WS = Wrapland::Server;

    auto keyboard = std::make_unique<input::keyboard>(&platform);
    auto keyboard_ptr = keyboard.get();

    QObject::connect(virtual_keyboard,
                     &WS::virtual_keyboard_v1::resourceDestroyed,
                     keyboard_ptr,
                     [this, virtual_keyboard] { virtual_keyboards.erase(virtual_keyboard); });

    QObject::connect(virtual_keyboard,
                     &WS::virtual_keyboard_v1::keymap,
                     keyboard_ptr,
                     [keyboard_ptr](auto /*format*/, auto fd, auto size) {
                         // TODO(romangg): Should we check the format?
                         keyboard_ptr->xkb->install_keymap(fd, size);
                     });

    QObject::connect(virtual_keyboard,
                     &WS::virtual_keyboard_v1::key,
                     keyboard_ptr,
                     [keyboard_ptr](auto time, auto key, auto state) {
                         Q_EMIT keyboard_ptr->key_changed({key,
                                                           state == WS::key_state::pressed
                                                               ? key_state::pressed
                                                               : key_state::released,
                                                           false,
                                                           keyboard_ptr,
                                                           time});
                     });

    QObject::connect(virtual_keyboard,
                     &WS::virtual_keyboard_v1::modifiers,
                     keyboard_ptr,
                     [keyboard_ptr](auto depressed, auto latched, auto locked, auto group) {
                         Q_EMIT keyboard_ptr->modifiers_changed(
                             {depressed, latched, locked, group, keyboard_ptr});
                     });

    virtual_keyboards.insert({virtual_keyboard, std::move(keyboard)});
    Q_EMIT platform.qobject->keyboard_added(keyboard_ptr);
}

}
