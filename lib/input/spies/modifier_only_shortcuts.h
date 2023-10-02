/*
SPDX-FileCopyrightText: 2016, 2017 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "desktop/screen_locker_watcher.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "input/keyboard.h"
#include "input/qt_event.h"
#include "input/xkb/helpers.h"
#include "kwin_export.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QObject>
#include <QSet>
#include <memory>

namespace KWin::input
{

class KWIN_EXPORT modifier_only_shortcuts_spy_qobject : public QObject
{
public:
    ~modifier_only_shortcuts_spy_qobject();
};

template<typename Redirect>
class modifier_only_shortcuts_spy : public event_spy<Redirect>
{
public:
    explicit modifier_only_shortcuts_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
        , qobject{std::make_unique<modifier_only_shortcuts_spy_qobject>()}
    {
        QObject::connect(redirect.space.screen_locker_watcher.get(),
                         &desktop::screen_locker_watcher::locked,
                         qobject.get(),
                         [this] { reset(); });
    }

    ~modifier_only_shortcuts_spy() = default;

    void key(key_event const& event) override
    {
        auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);

        if (event.state == key_state::pressed) {
            const bool wasEmpty = m_pressedKeys.isEmpty();
            m_pressedKeys.insert(event.keycode);
            if (wasEmpty && m_pressedKeys.size() == 1
                && !this->redirect.space.screen_locker_watcher->is_locked()
                && m_pressedButtons == Qt::NoButton && m_cachedMods == Qt::NoModifier) {
                m_modifier = Qt::KeyboardModifier(int(mods));
            } else {
                m_modifier = Qt::NoModifier;
            }
        } else if (!m_pressedKeys.isEmpty()) {
            m_pressedKeys.remove(event.keycode);
            if (m_pressedKeys.isEmpty() && mods == Qt::NoModifier
                && !this->redirect.space.global_shortcuts_disabled) {
                if (m_modifier != Qt::NoModifier) {
                    auto const list
                        = this->redirect.platform.base.options->modifierOnlyDBusShortcut(
                            m_modifier);
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

        m_cachedMods = xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(
            this->redirect.platform);
    }

    void button(button_event const& event) override
    {
        if (event.state == button_state::pressed) {
            m_pressedButtons |= button_to_qt_mouse_button(event.key);
        } else if (event.state == button_state::released) {
            m_pressedButtons &= ~button_to_qt_mouse_button(event.key);
        }
        reset();
    }

    void axis(axis_event const& /*event*/) override
    {
        reset();
    }

private:
    void reset()
    {
        m_modifier = Qt::NoModifier;
    }

    Qt::KeyboardModifier m_modifier = Qt::NoModifier;
    Qt::KeyboardModifiers m_cachedMods;
    Qt::MouseButtons m_pressedButtons;
    QSet<quint32> m_pressedKeys;

    std::unique_ptr<modifier_only_shortcuts_spy_qobject> qobject;
};

}
