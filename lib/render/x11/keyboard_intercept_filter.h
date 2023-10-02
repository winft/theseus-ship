/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "base/x11/xcb/qt_types.h"

#include <QKeyEvent>
#include <QtGui/private/qxkbcommon_p.h>
#include <xkbcommon/xkbcommon.h>

namespace KWin::render::x11
{

template<typename Effects, typename Xkb>
class keyboard_intercept_filter : public base::x11::event_filter
{
public:
    keyboard_intercept_filter(Effects& effects, Xkb const& xkb)
        : base::x11::event_filter(*effects.scene.compositor.platform.base.x11_event_filters,
                                  QVector<int>{XCB_KEY_PRESS, XCB_KEY_RELEASE})
        , xkb{xkb}
        , effects{effects}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        switch (event->response_type & ~0x80) {
        case XCB_KEY_PRESS: {
            auto key_event = reinterpret_cast<xcb_key_press_event_t*>(event);
            handle_key_event(QEvent::KeyPress, key_event->detail, key_event->time);
            return true;
        }
        case XCB_KEY_RELEASE: {
            auto const key_event = reinterpret_cast<xcb_key_release_event_t*>(event);
            handle_key_event(QEvent::KeyRelease, key_event->detail, key_event->time);
            return true;
        }
        default:
            return false;
        }
    }

private:
    void handle_key_event(QEvent::Type event_type, xcb_keycode_t keycode, xcb_timestamp_t timestamp)
    {
        auto keysym = xkb_state_key_get_one_sym(xkb.state, keycode);

        auto modifiers = xkb.qt_modifiers;
        if (QXkbCommon::isKeypad(keysym)) {
            modifiers |= Qt::KeypadModifier;
        }

        auto const qt_key = xkb.to_qt_key(keysym, keycode, modifiers, false);
        auto const text = QXkbCommon::lookupString(xkb.state, keycode);

        QKeyEvent event(event_type, qt_key, modifiers, text);
        event.setTimestamp(timestamp);

        effects.grabbedKeyboardEvent(&event);
    }

    Xkb const& xkb;
    Effects& effects;
};

}
