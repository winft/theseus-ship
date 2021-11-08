/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcut.h"

#include "input/event.h"
#include "input/global_shortcuts_manager.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/xkb_helpers.h"
#include "main.h"

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
        auto mods = get_active_keyboard_modifiers(kwinApp()->input.get());
        if (redirect->shortcuts()->processPointerPressed(mods, redirect->qtButtonStates())) {
            return true;
        }
    }
    return false;
}

bool global_shortcut_filter::axis(axis_event const& event)
{
    auto mods = get_active_keyboard_modifiers(kwinApp()->input);

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

bool global_shortcut_filter::key(key_event const& event)
{
    auto xkb = event.base.dev->xkb.get();
    auto const& modifiers = xkb->qt_modifiers;
    auto const& shortcuts = kwinApp()->input->redirect->shortcuts();
    auto qt_key = key_to_qt_key(event.keycode, xkb);

    auto handle_power_key = [this, state = event.state, shortcuts, modifiers, qt_key] {
        auto power_off = [this, shortcuts, modifiers] {
            QObject::disconnect(m_powerDown, &QTimer::timeout, shortcuts, nullptr);
            m_powerDown->stop();
            shortcuts->processKey(modifiers, Qt::Key_PowerDown);
        };

        switch (state) {
        case key_state::pressed:
            QObject::connect(m_powerDown, &QTimer::timeout, shortcuts, power_off);
            m_powerDown->start();
            return true;
        case key_state::released:
            auto const ret = !m_powerDown->isActive() || shortcuts->processKey(modifiers, qt_key);
            m_powerDown->stop();
            return ret;
        }
        return false;
    };

    if (qt_key == Qt::Key_PowerOff) {
        return handle_power_key();
    }

    if (event.state == key_state::pressed) {
        return shortcuts->processKey(modifiers, qt_key);
    }

    return false;
}

bool global_shortcut_filter::key_repeat(key_event const& event)
{
    auto xkb = event.base.dev->xkb.get();
    auto qt_key = key_to_qt_key(event.keycode, xkb);

    if (qt_key == Qt::Key_PowerOff) {
        return false;
    }

    auto const& redirect = kwinApp()->input->redirect;
    auto const& modifiers = xkb->modifiers_relevant_for_global_shortcuts();

    return redirect->shortcuts()->processKey(modifiers, qt_key);
}

bool global_shortcut_filter::swipe_begin(swipe_begin_event const& event)
{
    kwinApp()->input->redirect->shortcuts()->processSwipeStart(event.fingers);
    return false;
}

bool global_shortcut_filter::swipe_update(swipe_update_event const& event)
{
    kwinApp()->input->redirect->shortcuts()->processSwipeUpdate(
        QSizeF(event.delta.x(), event.delta.y()));
    return false;
}

bool global_shortcut_filter::swipe_end(swipe_end_event const& event)
{
    if (event.cancelled) {
        kwinApp()->input->redirect->shortcuts()->processSwipeCancel();
    } else {
        kwinApp()->input->redirect->shortcuts()->processSwipeEnd();
    }
    return false;
}

}
