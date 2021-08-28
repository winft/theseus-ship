/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcut.h"

#include "input/event.h"
#include "input/global_shortcuts_manager.h"
#include "main.h"
#include <input/keyboard_redirect.h>

#include <QKeyEvent>
#include <QTimer>

namespace KWin::input
{

global_shortcut_filter::global_shortcut_filter()
{
    m_powerDown = new QTimer;
    m_powerDown->setSingleShot(true);
    m_powerDown->setInterval(1000);
}

global_shortcut_filter::~global_shortcut_filter()
{
    delete m_powerDown;
}

bool global_shortcut_filter::button(button_event const& event)
{
    if (event.state == button_state::pressed) {
        auto redirect = kwinApp()->input->redirect.get();
        if (redirect->shortcuts()->processPointerPressed(redirect->keyboardModifiers(),
                                                         redirect->qtButtonStates())) {
            return true;
        }
    }
    return false;
}

bool global_shortcut_filter::axis(axis_event const& event)
{
    auto mods = kwinApp()->input->redirect->keyboard()->modifiers();

    if (mods == Qt::NoModifier) {
        return false;
    }

    auto direction = PointerAxisUp;
    if (event.orientation == axis_orientation::horizontal) {
        // TODO(romangg): Doesn't < 0 equal left direction?
        direction = event.delta < 0 ? PointerAxisRight : PointerAxisLeft;
    } else if (event.delta < 0) {
        direction = PointerAxisDown;
    }

    return kwinApp()->input->redirect->shortcuts()->processAxis(mods, direction);
}

bool global_shortcut_filter::keyEvent(QKeyEvent* event)
{
    auto const& redirect = kwinApp()->input->redirect;
    auto const& modifiers = static_cast<KeyEvent*>(event)->modifiersRelevantForGlobalShortcuts();
    auto const& shortcuts = redirect->shortcuts();

    auto handle_power_key = [this, event, shortcuts, modifiers] {
        auto power_off = [this, shortcuts, modifiers] {
            QObject::disconnect(m_powerDown, &QTimer::timeout, shortcuts, nullptr);
            m_powerDown->stop();
            shortcuts->processKey(modifiers, Qt::Key_PowerDown);
        };

        switch (event->type()) {
        case QEvent::KeyPress:
            QObject::connect(m_powerDown, &QTimer::timeout, shortcuts, power_off);
            m_powerDown->start();
            return true;
        case QEvent::KeyRelease:
            auto const ret
                = !m_powerDown->isActive() || shortcuts->processKey(modifiers, event->key());
            m_powerDown->stop();
            return ret;
        }
        return false;
    };

    if (event->key() == Qt::Key_PowerOff) {
        return handle_power_key();
    }

    if (event->type() == QEvent::KeyPress) {
        return shortcuts->processKey(modifiers, event->key());
    }

    return false;
}

bool global_shortcut_filter::key_repeat(QKeyEvent* event)
{
    if (event->key() == Qt::Key_PowerOff) {
        return false;
    }
    return kwinApp()->input->redirect->shortcuts()->processKey(
        static_cast<KeyEvent*>(event)->modifiersRelevantForGlobalShortcuts(), event->key());
}

bool global_shortcut_filter::swipeGestureBegin(int fingerCount, quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input->redirect->shortcuts()->processSwipeStart(fingerCount);
    return false;
}

bool global_shortcut_filter::swipeGestureUpdate(QSizeF const& delta, quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input->redirect->shortcuts()->processSwipeUpdate(delta);
    return false;
}

bool global_shortcut_filter::swipeGestureCancelled(quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input->redirect->shortcuts()->processSwipeCancel();
    return false;
}

bool global_shortcut_filter::swipeGestureEnd(quint32 time)
{
    Q_UNUSED(time)
    kwinApp()->input->redirect->shortcuts()->processSwipeEnd();
    return false;
}

}
