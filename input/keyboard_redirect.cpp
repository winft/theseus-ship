/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "keyboard_redirect.h"

#include "event_filter.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "input/spies/modifier_only_shortcuts.h"
#include "screenlockerwatcher.h"
#include "spies/keyboard_layout.h"
#include "spies/keyboard_repeat.h"
#include "toplevel.h"
#include "utils.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/stacking_order.h"
#include "win/wayland/window.h"

// Wrapland
#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/seat.h>
// screenlocker
#include <KScreenLocker/KsldApp>
// Frameworks
#include <KGlobalAccel>
// Qt
#include <QKeyEvent>

namespace KWin::input
{

keyboard_redirect::keyboard_redirect(input::redirect* redirect)
    : QObject()
    , m_input(redirect)
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
    KeyStateChangedSpy(input::redirect* input)
        : m_input(input)
    {
    }

    void keyEvent(KeyEvent* event) override
    {
        if (event->isAutoRepeat()) {
            return;
        }
        emit m_input->keyStateChanged(event->nativeScanCode(),
                                      event->type() == QEvent::KeyPress
                                          ? input::redirect::KeyboardKeyPressed
                                          : input::redirect::KeyboardKeyReleased);
    }

private:
    input::redirect* m_input;
};

class modifiers_changed_spy : public event_spy
{
public:
    modifiers_changed_spy(input::redirect* input)
        : m_input(input)
        , m_modifiers()
    {
    }

    void keyEvent(KeyEvent* event) override
    {
        if (event->isAutoRepeat()) {
            return;
        }
        updateModifiers(event->modifiers());
    }

    void updateModifiers(Qt::KeyboardModifiers mods)
    {
        if (mods == m_modifiers) {
            return;
        }
        emit m_input->keyboardModifiersChanged(mods, m_modifiers);
        m_modifiers = mods;
    }

private:
    input::redirect* m_input;
    Qt::KeyboardModifiers m_modifiers;
};

void keyboard_redirect::init()
{
    Q_ASSERT(!m_inited);
    m_inited = true;
    const auto config = kwinApp()->kxkbConfig();
    m_xkb->setNumLockConfig(kwinApp()->inputConfig());
    m_xkb->setConfig(config);

    m_input->installInputEventSpy(new KeyStateChangedSpy(m_input));
    modifiers_spy = new modifiers_changed_spy(m_input);
    m_input->installInputEventSpy(modifiers_spy);
    m_keyboardLayout = new keyboard_layout_spy(m_xkb.data(), config);
    m_keyboardLayout->init();
    m_input->installInputEventSpy(m_keyboardLayout);

    if (waylandServer()->hasGlobalShortcutSupport()) {
        m_input->installInputEventSpy(new modifier_only_shortcuts_spy);
    }

    auto keyRepeatSpy = new keyboard_repeat_spy(m_xkb.data());
    connect(keyRepeatSpy,
            &keyboard_repeat_spy::keyRepeat,
            this,
            std::bind(&keyboard_redirect::processKey,
                      this,
                      std::placeholders::_1,
                      input::redirect::KeyboardKeyAutoRepeat,
                      std::placeholders::_2,
                      nullptr));
    m_input->installInputEventSpy(keyRepeatSpy);

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
        if (found->surface() != seat->focusedKeyboardSurface()) {
            seat->setFocusedKeyboardSurface(found->surface());
        }
    } else {
        seat->setFocusedKeyboardSurface(nullptr);
    }
}

void keyboard_redirect::processKey(uint32_t key,
                                   input::redirect::KeyboardKeyState state,
                                   uint32_t time,
                                   input::keyboard* device)
{
    QEvent::Type type;
    bool autoRepeat = false;
    switch (state) {
    case input::redirect::KeyboardKeyAutoRepeat:
        autoRepeat = true;
        // fall through
    case input::redirect::KeyboardKeyPressed:
        type = QEvent::KeyPress;
        break;
    case input::redirect::KeyboardKeyReleased:
        type = QEvent::KeyRelease;
        break;
    default:
        Q_UNREACHABLE();
    }

    const quint32 previousLayout = m_xkb->currentLayout();
    if (!autoRepeat) {
        m_xkb->updateKey(key, state);
    }

    const xkb_keysym_t keySym = m_xkb->currentKeysym();
    const Qt::KeyboardModifiers globalShortcutsModifiers
        = m_xkb->modifiersRelevantForGlobalShortcuts(key);
    KeyEvent event(
        type,
        m_xkb->toQtKey(
            keySym, key, globalShortcutsModifiers ? Qt::ControlModifier : Qt::KeyboardModifiers()),
        m_xkb->modifiers(),
        key,
        keySym,
        m_xkb->toString(keySym),
        autoRepeat,
        time,
        device);
    event.setModifiersRelevantForGlobalShortcuts(globalShortcutsModifiers);

    m_input->processSpies(std::bind(&event_spy::keyEvent, std::placeholders::_1, &event));
    if (!m_inited) {
        return;
    }
    m_input->processFilters(
        std::bind(&input::event_filter::keyEvent, std::placeholders::_1, &event));

    m_xkb->forwardModifiers();

    if (event.modifiersRelevantForGlobalShortcuts() == Qt::KeyboardModifier::NoModifier
        && type != QEvent::KeyRelease) {
        m_keyboardLayout->checkLayoutChange(previousLayout);
    }
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
