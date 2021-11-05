/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "redirect.h"

#include "input/event.h"
#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/spies/keyboard_layout.h"
#include "input/spies/keyboard_repeat.h"
#include "input/spies/modifier_only_shortcuts.h"

#include "main.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"
#include "workspace.h"

#include <KScreenLocker/KsldApp>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

namespace KWin::input::wayland
{

keyboard_redirect::keyboard_redirect(wayland::redirect* redirect)
    : input::keyboard_redirect(redirect)
{
    m_xkb->setSeat(waylandServer()->seat());
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

    void key(key_event const& /*event*/) override
    {
        updateModifiers(kwinApp()->input->redirect->keyboardModifiers());
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
    assert(!m_inited);
    m_inited = true;

    const auto config = kwinApp()->kxkbConfig();
    m_xkb->setNumLockConfig(kwinApp()->inputConfig());
    m_xkb->setConfig(config);

    redirect->installInputEventSpy(new KeyStateChangedSpy(redirect));
    modifiers_spy = new modifiers_changed_spy(redirect);
    redirect->installInputEventSpy(modifiers_spy);
    m_keyboardLayout = new keyboard_layout_spy(m_xkb.get(), config);
    m_keyboardLayout->init();
    redirect->installInputEventSpy(m_keyboardLayout);

    if (waylandServer()->hasGlobalShortcutSupport()) {
        redirect->installInputEventSpy(new modifier_only_shortcuts_spy);
    }

    auto keyRepeatSpy = new keyboard_repeat_spy(m_xkb.get());
    connect(keyRepeatSpy,
            &keyboard_repeat_spy::keyRepeat,
            this,
            &keyboard_redirect::process_key_repeat);
    redirect->installInputEventSpy(keyRepeatSpy);

    connect(workspace(), &QObject::destroyed, this, [this] { m_inited = false; });
    connect(waylandServer(), &QObject::destroyed, this, [this] { m_inited = false; });
    connect(workspace(), &Workspace::clientActivated, this, [this] {
        disconnect(m_activeClientSurfaceChangedConnection);
        if (auto c = workspace()->activeClient()) {
            m_activeClientSurfaceChangedConnection
                = connect(c, &Toplevel::surfaceChanged, this, &keyboard_redirect::update);
        } else {
            m_activeClientSurfaceChangedConnection = QMetaObject::Connection();
        }
        update();
    });
    if (waylandServer()->hasScreenLockerIntegration()) {
        connect(ScreenLocker::KSldApp::self(),
                &ScreenLocker::KSldApp::lockStateChanged,
                this,
                &keyboard_redirect::update);
    }
}

void keyboard_redirect::update()
{
    if (!m_inited) {
        return;
    }
    auto seat = waylandServer()->seat();

    // TODO: this needs better integration
    Toplevel* found = nullptr;
    auto const& stacking = workspace()->stacking_order->sorted();
    if (!stacking.empty()) {
        auto it = stacking.end();
        do {
            --it;
            Toplevel* t = (*it);
            if (t->isDeleted()) {
                // a deleted window doesn't get mouse events
                continue;
            }
            if (!t->readyForPainting()) {
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
        found = workspace()->activeClient();
    }
    if (found && found->surface()) {
        if (found->surface() != seat->keyboards().get_focus().surface) {
            seat->setFocusedKeyboardSurface(found->surface());
        }
    } else {
        seat->setFocusedKeyboardSurface(nullptr);
    }
}

void keyboard_redirect::process_key(key_event const& event)
{
    auto const previousLayout = m_xkb->currentLayout();

    input::keyboard_redirect::process_key(event);

    if (!m_inited) {
        return;
    }

    auto const globalShortcutsModifiers = m_xkb->modifiersRelevantForGlobalShortcuts(event.keycode);

    redirect->processFilters(std::bind(&event_filter::key, std::placeholders::_1, event));
    m_xkb->forwardModifiers();

    if (globalShortcutsModifiers == Qt::KeyboardModifier::NoModifier
        && event.state == key_state::pressed) {
        m_keyboardLayout->checkLayoutChange(previousLayout);
    }
}

void keyboard_redirect::process_key_repeat(uint32_t key, uint32_t time)
{
    input::keyboard_redirect::process_key_repeat(key, time);

    if (!m_inited) {
        return;
    }

    auto event = key_event{key, key_state::pressed, false, nullptr, time};
    redirect->processFilters(std::bind(&event_filter::key_repeat, std::placeholders::_1, event));
}

void keyboard_redirect::processModifiers(uint32_t modsDepressed,
                                         uint32_t modsLatched,
                                         uint32_t modsLocked,
                                         uint32_t group)
{
    if (!m_inited) {
        return;
    }
    auto const previousLayout = m_xkb->currentLayout();
    // TODO: send to proper Client and also send when active Client changes
    m_xkb->updateModifiers(modsDepressed, modsLatched, modsLocked, group);
    modifiers_spy->updateModifiers(modifiers());
    m_keyboardLayout->checkLayoutChange(previousLayout);
}

void keyboard_redirect::processKeymapChange(int fd, uint32_t size)
{
    if (!m_inited) {
        return;
    }
    // TODO: should we pass the keymap to our Clients? Or only to the currently active one and
    // update
    m_xkb->installKeymap(fd, size);
    m_keyboardLayout->resetLayout();
}

}
