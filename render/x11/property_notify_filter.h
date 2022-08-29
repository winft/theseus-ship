/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window_find.h"

namespace KWin::render::x11
{

template<typename Effects, typename Space>
class property_notify_filter : public base::x11::event_filter
{
public:
    property_notify_filter(Effects& effects, Space& space, xcb_window_t root_window)
        : base::x11::event_filter(QVector<int>{XCB_PROPERTY_NOTIFY})
        , effects{effects}
        , space{space}
        , root_window{root_window}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        auto pe = reinterpret_cast<xcb_property_notify_event_t*>(event);
        if (!effects.registered_atoms.contains(pe->atom)) {
            return false;
        }

        if (pe->window == root_window) {
            Q_EMIT effects.propertyNotify(nullptr, pe->atom);
        } else if (const auto c = win::x11::find_controlled_window<typename Space::x11_window>(
                       space, win::x11::predicate_match::window, pe->window)) {
            Q_EMIT effects.propertyNotify(c->render->effect.get(), pe->atom);
        } else if (const auto c
                   = win::x11::find_unmanaged<typename Space::x11_window>(space, pe->window)) {
            Q_EMIT effects.propertyNotify(c->render->effect.get(), pe->atom);
        }

        return false;
    }

private:
    Effects& effects;
    Space& space;
    xcb_window_t root_window;
};

}
