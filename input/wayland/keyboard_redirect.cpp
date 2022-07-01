/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "redirect.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/keyboard.h"
#include "input/spies/keyboard_repeat.h"
#include "input/spies/modifier_only_shortcuts.h"
#include "input/xkb/helpers.h"
#include "input/xkb/layout_manager.h"
#include "main.h"
#include "toplevel.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"

#include <KScreenLocker/KsldApp>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin::input::wayland
{

keyboard_redirect::keyboard_redirect(wayland::redirect* redirect)
    : input::keyboard_redirect(redirect)
{
}

class KeyStateChangedSpy : public event_spy
{
public:
    KeyStateChangedSpy(input::redirect* redirect)
        : redirect(redirect)
    {
    }

    void key(key_event const& event) override
    {
        Q_EMIT redirect->keyStateChanged(event.keycode, event.state);
    }

private:
    input::redirect* redirect;
};

class modifiers_changed_spy : public event_spy
{
public:
    modifiers_changed_spy(input::redirect* redirect)
        : redirect{redirect}
        , m_modifiers()
    {
    }

    void key(key_event const& event) override
    {
        if (auto& xkb = event.base.dev->xkb) {
            updateModifiers(xkb->qt_modifiers);
        }
    }

    void updateModifiers(Qt::KeyboardModifiers mods)
    {
        if (mods == m_modifiers) {
            return;
        }
        Q_EMIT redirect->keyboardModifiersChanged(mods, m_modifiers);
        m_modifiers = mods;
    }

private:
    input::redirect* redirect;
    Qt::KeyboardModifiers m_modifiers;
};

keyboard_redirect::~keyboard_redirect() = default;

void keyboard_redirect::init()
{
    auto& xkb = kwinApp()->input->xkb;
    auto const config = kwinApp()->kxkbConfig();
    xkb.setNumLockConfig(kwinApp()->inputConfig());
    xkb.setConfig(config);

    redirect->installInputEventSpy(new KeyStateChangedSpy(redirect));
    modifiers_spy = new modifiers_changed_spy(redirect);
    redirect->installInputEventSpy(modifiers_spy);

    layout_manager = std::make_unique<xkb::layout_manager>(kwinApp()->input->xkb, config);
    layout_manager->init();

    if (waylandServer()->has_global_shortcut_support()) {
        redirect->installInputEventSpy(new modifier_only_shortcuts_spy(*redirect));
    }

    auto keyRepeatSpy = new keyboard_repeat_spy();
    QObject::connect(keyRepeatSpy,
                     &keyboard_repeat_spy::key_repeated,
                     this,
                     &keyboard_redirect::process_key_repeat);
    redirect->installInputEventSpy(keyRepeatSpy);

    QObject::connect(&redirect->space, &win::space::clientActivated, this, [this] {
        QObject::disconnect(m_activeClientSurfaceChangedConnection);
        if (auto c = redirect->space.activeClient()) {
            m_activeClientSurfaceChangedConnection
                = QObject::connect(c, &Toplevel::surfaceChanged, this, &keyboard_redirect::update);
        } else {
            m_activeClientSurfaceChangedConnection = QMetaObject::Connection();
        }
        update();
    });
    if (waylandServer()->has_screen_locker_integration()) {
        QObject::connect(ScreenLocker::KSldApp::self(),
                         &ScreenLocker::KSldApp::lockStateChanged,
                         this,
                         &keyboard_redirect::update);
    }
}

void keyboard_redirect::update()
{
    auto seat = waylandServer()->seat();
    if (!seat->hasKeyboard()) {
        return;
    }

    // TODO: this needs better integration
    Toplevel* found = nullptr;
    auto const& stacking = redirect->space.stacking_order->stack;
    if (!stacking.empty()) {
        auto it = stacking.end();
        do {
            --it;
            Toplevel* t = (*it);
            if (t->remnant) {
                // a deleted window doesn't get mouse events
                continue;
            }
            if (!t->ready_for_painting) {
                continue;
            }
            auto wayland_window = qobject_cast<win::wayland::window*>(t);
            if (!wayland_window) {
                continue;
            }
            if (!wayland_window->layer_surface
                || !wayland_window->has_exclusive_keyboard_interactivity()) {
                continue;
            }
            found = t;
            break;
        } while (it != stacking.begin());
    }

    if (!found && !kwinApp()->input->redirect->isSelectingWindow()) {
        found = redirect->space.activeClient();
    }
    if (found && found->surface) {
        if (found->surface != seat->keyboards().get_focus().surface) {
            seat->setFocusedKeyboardSurface(found->surface);
        }
    } else {
        seat->setFocusedKeyboardSurface(nullptr);
    }
}

void keyboard_redirect::process_key(key_event const& event)
{
    auto& xkb = event.base.dev->xkb;

    input::keyboard_redirect::process_key(event);

    redirect->processFilters(std::bind(&event_filter::key, std::placeholders::_1, event));
    xkb->forward_modifiers();
}

void keyboard_redirect::process_key_repeat(const key_event& event)
{
    input::keyboard_redirect::process_key_repeat(event);
    redirect->processFilters(std::bind(&event_filter::key_repeat, std::placeholders::_1, event));
}

void keyboard_redirect::process_modifiers(modifiers_event const& event)
{
    auto const& xkb = event.base.dev->xkb.get();

    // TODO: send to proper Client and also send when active Client changes
    xkb->update_modifiers(event.depressed, event.latched, event.locked, event.group);

    modifiers_spy->updateModifiers(xkb->qt_modifiers);
}

}
