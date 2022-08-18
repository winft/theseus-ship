/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/event_filter.h"
#include "input/global_shortcuts_manager.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/qt_event.h"
#include "input/xkb/helpers.h"
#include "main.h"

#include <QTimer>

namespace KWin::input
{

template<typename Redirect>
class global_shortcut_filter : public event_filter<Redirect>
{
public:
    explicit global_shortcut_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
        m_powerDown = new QTimer;
        m_powerDown->setSingleShot(true);
        m_powerDown->setInterval(1000);
    }

    ~global_shortcut_filter() override
    {
        delete m_powerDown;
    }

    bool button(button_event const& event) override
    {
        if (event.state == button_state::pressed) {
            auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);
            if (this->redirect.platform.shortcuts->processPointerPressed(
                    mods, this->redirect.qtButtonStates())) {
                return true;
            }
        }
        return false;
    }

    bool axis(axis_event const& event) override
    {
        auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);

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

        return this->redirect.platform.shortcuts->processAxis(mods, direction);
    }

    bool key(key_event const& event) override
    {
        auto xkb = event.base.dev->xkb.get();
        auto const& modifiers = xkb->qt_modifiers;
        auto const& shortcuts = this->redirect.platform.shortcuts.get();
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
                auto const ret
                    = !m_powerDown->isActive() || shortcuts->processKey(modifiers, qt_key);
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
        } else {
            return shortcuts->processKeyRelease(modifiers, qt_key);
        }
    }

    bool key_repeat(key_event const& event) override
    {
        auto xkb = event.base.dev->xkb.get();
        auto qt_key = key_to_qt_key(event.keycode, xkb);

        if (qt_key == Qt::Key_PowerOff) {
            return false;
        }

        auto const& modifiers = xkb->modifiers_relevant_for_global_shortcuts();
        return this->redirect.platform.shortcuts->processKey(modifiers, qt_key);
    }

    bool swipe_begin(swipe_begin_event const& event) override
    {
        this->redirect.platform.shortcuts->processSwipeStart(event.fingers);
        return false;
    }

    bool swipe_update(swipe_update_event const& event) override
    {
        this->redirect.platform.shortcuts->processSwipeUpdate(
            QSizeF(event.delta.x(), event.delta.y()));
        return false;
    }

    bool swipe_end(swipe_end_event const& event) override
    {
        if (event.cancelled) {
            this->redirect.platform.shortcuts->processSwipeCancel();
        } else {
            this->redirect.platform.shortcuts->processSwipeEnd();
        }
        return false;
    }

private:
    QTimer* m_powerDown = nullptr;
};

}
