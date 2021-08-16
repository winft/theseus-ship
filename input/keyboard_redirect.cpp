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

    void keyEvent(KeyEvent* event) override
    {
        if (event->isAutoRepeat()) {
            return;
        }
        Q_EMIT redirect->keyStateChanged(event->nativeScanCode(),
                                         event->type() == QEvent::KeyPress
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
            std::bind(&keyboard_redirect::processKey,
                      this,
                      std::placeholders::_1,
                      redirect::KeyboardKeyAutoRepeat,
                      std::placeholders::_2,
                      nullptr));
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
        if (found->surface() != seat->focusedKeyboardSurface()) {
            seat->setFocusedKeyboardSurface(found->surface());
        }
    } else {
        seat->setFocusedKeyboardSurface(nullptr);
    }
}

void keyboard_redirect::process_key(key_event const& event)
{
    processKey(event.keycode,
               (redirect::KeyboardKeyState)event.state,
               event.base.time_msec,
               event.base.dev);
}

void keyboard_redirect::processKey(uint32_t key,
                                   redirect::KeyboardKeyState state,
                                   uint32_t time,
                                   keyboard* device)
{
    QEvent::Type type;
    bool autoRepeat = false;
    switch (state) {
    case redirect::KeyboardKeyAutoRepeat:
        autoRepeat = true;
        // fall through
    case redirect::KeyboardKeyPressed:
        type = QEvent::KeyPress;
        break;
    case redirect::KeyboardKeyReleased:
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

    redirect->processSpies(std::bind(&event_spy::keyEvent, std::placeholders::_1, &event));
    if (!m_inited) {
        return;
    }
    redirect->processFilters(std::bind(&event_filter::keyEvent, std::placeholders::_1, &event));

    m_xkb->forwardModifiers();

    if (event.modifiersRelevantForGlobalShortcuts() == Qt::KeyboardModifier::NoModifier
        && type != QEvent::KeyRelease) {
        m_keyboardLayout->checkLayoutChange(previousLayout);
    }
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
