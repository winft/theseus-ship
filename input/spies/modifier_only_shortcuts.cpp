/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2016, 2017 Martin Gräßlin <mgraesslin@kde.org>

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
#include "modifier_only_shortcuts.h"

#include "desktop/screen_locker_watcher.h"
#include "input/event.h"
#include "input/keyboard.h"
#include "input/qt_event.h"
#include "input/xkb/helpers.h"
#include "options.h"
#include "win/space.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

namespace KWin::input
{

modifier_only_shortcuts_spy::modifier_only_shortcuts_spy()
    : QObject()
    , event_spy()
{
    QObject::connect(kwinApp()->screen_locker_watcher.get(),
                     &desktop::screen_locker_watcher::locked,
                     this,
                     &modifier_only_shortcuts_spy::reset);
}

modifier_only_shortcuts_spy::~modifier_only_shortcuts_spy() = default;

void modifier_only_shortcuts_spy::key(key_event const& event)
{
    auto mods = xkb::get_active_keyboard_modifiers(kwinApp()->input.get());

    if (event.state == key_state::pressed) {
        const bool wasEmpty = m_pressedKeys.isEmpty();
        m_pressedKeys.insert(event.keycode);
        if (wasEmpty && m_pressedKeys.size() == 1 && !kwinApp()->screen_locker_watcher->is_locked()
            && m_pressedButtons == Qt::NoButton && m_cachedMods == Qt::NoModifier) {
            m_modifier = Qt::KeyboardModifier(int(mods));
        } else {
            m_modifier = Qt::NoModifier;
        }
    } else if (!m_pressedKeys.isEmpty()) {
        m_pressedKeys.remove(event.keycode);
        if (m_pressedKeys.isEmpty() && mods == Qt::NoModifier
            && !workspace()->globalShortcutsDisabled()) {
            if (m_modifier != Qt::NoModifier) {
                const auto list = options->modifierOnlyDBusShortcut(m_modifier);
                if (list.size() >= 4) {
                    auto call = QDBusMessage::createMethodCall(
                        list.at(0), list.at(1), list.at(2), list.at(3));
                    QVariantList args;
                    for (int i = 4; i < list.size(); ++i) {
                        args << list.at(i);
                    }
                    call.setArguments(args);
                    QDBusConnection::sessionBus().asyncCall(call);
                }
            }
        }
        m_modifier = Qt::NoModifier;
    } else {
        m_modifier = Qt::NoModifier;
    }

    m_cachedMods
        = xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(kwinApp()->input.get());
}

void modifier_only_shortcuts_spy::button(button_event const& event)
{
    if (event.state == button_state::pressed) {
        m_pressedButtons |= button_to_qt_mouse_button(event.key);
    } else if (event.state == button_state::released) {
        m_pressedButtons &= ~button_to_qt_mouse_button(event.key);
    }
    reset();
}

void modifier_only_shortcuts_spy::axis([[maybe_unused]] axis_event const& event)
{
    reset();
}

}
