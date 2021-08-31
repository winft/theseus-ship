/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "event_filter.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "input/spies/modifier_only_shortcuts.h"
#include "spies/keyboard_layout.h"
#include "spies/keyboard_repeat.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/stacking_order.h"
#include "win/wayland/window.h"

#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <KGlobalAccel>
#include <KScreenLocker/KsldApp>
#include <QKeyEvent>

namespace KWin::input
{

keyboard_redirect::keyboard_redirect(input::redirect* redirect)
    : QObject()
    , redirect(redirect)
    , m_xkb(new input::xkb(redirect))
{
    connect(m_xkb.data(), &input::xkb::ledsChanged, this, &keyboard_redirect::ledsChanged);
    if (waylandServer()) {
        m_xkb->setSeat(waylandServer()->seat());
    }
}

keyboard_redirect::~keyboard_redirect() = default;

class KeyStateChangedSpy : public event_spy
{
public:
    KeyStateChangedSpy(input::redirect* redirect)
        : redirect(redirect)
    {
    }

    void key(key_event const& event) override
    {
        Q_EMIT redirect->keyStateChanged(event.keycode,
                                         event.state == button_state::pressed
                                             ? redirect::KeyboardKeyPressed
                                             : redirect::KeyboardKeyReleased);
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

void keyboard_redirect::init()
{
    Q_ASSERT(!m_inited);
    m_inited = true;
    const auto config = kwinApp()->kxkbConfig();
    m_xkb->setNumLockConfig(kwinApp()->inputConfig());
    m_xkb->setConfig(config);

    redirect->installInputEventSpy(new KeyStateChangedSpy(redirect));
    modifiers_spy = new modifiers_changed_spy(redirect);
    redirect->installInputEventSpy(modifiers_spy);
    m_keyboardLayout = new keyboard_layout_spy(m_xkb.data(), config);
    m_keyboardLayout->init();
    redirect->installInputEventSpy(m_keyboardLayout);

    if (waylandServer()->hasGlobalShortcutSupport()) {
        redirect->installInputEventSpy(new modifier_only_shortcuts_spy);
    }

    auto keyRepeatSpy = new keyboard_repeat_spy(m_xkb.data());
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

input::xkb* keyboard_redirect::xkb() const
{
    return m_xkb.data();
}
Qt::KeyboardModifiers keyboard_redirect::modifiers() const
{
    return m_xkb->modifiers();
}
Qt::KeyboardModifiers keyboard_redirect::modifiersRelevantForGlobalShortcuts() const
{
    return m_xkb->modifiersRelevantForGlobalShortcuts();
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
        if (found->surface() != seat->keyboards().focus.surface) {
            seat->setFocusedKeyboardSurface(found->surface());
        }
    } else {
        seat->setFocusedKeyboardSurface(nullptr);
    }
}

void keyboard_redirect::process_key(key_event const& event)
{
    const quint32 previousLayout = m_xkb->currentLayout();
    m_xkb->updateKey(event.keycode, (redirect::KeyboardKeyState)event.state);

    const Qt::KeyboardModifiers globalShortcutsModifiers
        = m_xkb->modifiersRelevantForGlobalShortcuts(event.keycode);

    redirect->processSpies(std::bind(&event_spy::key, std::placeholders::_1, event));
    if (!m_inited) {
        return;
    }
    redirect->processFilters(std::bind(&event_filter::key, std::placeholders::_1, event));

    m_xkb->forwardModifiers();

    if (globalShortcutsModifiers == Qt::KeyboardModifier::NoModifier
        && event.state == button_state::pressed) {
        m_keyboardLayout->checkLayoutChange(previousLayout);
    }
}

void keyboard_redirect::process_key_repeat(uint32_t key, uint32_t time)
{
    auto event = key_event{key, button_state::pressed, false, nullptr, time};

    redirect->processSpies(std::bind(&event_spy::key_repeat, std::placeholders::_1, event));
    if (!m_inited) {
        return;
    }
    redirect->processFilters(std::bind(&event_filter::key_repeat, std::placeholders::_1, event));
}

void keyboard_redirect::process_modifiers(modifiers_event const& event)
{
    processModifiers(event.depressed, event.latched, event.locked, event.group);
}

void keyboard_redirect::processModifiers(uint32_t modsDepressed,
                                         uint32_t modsLatched,
                                         uint32_t modsLocked,
                                         uint32_t group)
{
    if (!m_inited) {
        return;
    }
    const quint32 previousLayout = m_xkb->currentLayout();
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
