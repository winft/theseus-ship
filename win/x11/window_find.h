/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QObject>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Win, typename Space>
Win* find_controlled_window(Space& space, predicate_match predicate, xcb_window_t w)
{
    switch (predicate) {
    case predicate_match::window:
        return qobject_cast<Win*>(space.findAbstractClient([w](auto const* c) {
            auto x11_client = qobject_cast<Win const*>(c);
            return x11_client && x11_client->xcb_window() == w;
        }));
    case predicate_match::wrapper_id:
        return qobject_cast<Win*>(space.findAbstractClient([w](auto const* c) {
            auto x11_client = qobject_cast<Win const*>(c);
            return x11_client && x11_client->xcb_windows.wrapper == w;
        }));
    case predicate_match::frame_id:
        return qobject_cast<Win*>(space.findAbstractClient([w](auto const* c) {
            auto x11_client = qobject_cast<Win const*>(c);
            return x11_client && x11_client->xcb_windows.outer == w;
        }));
    case predicate_match::input_id:
        return qobject_cast<Win*>(space.findAbstractClient([w](auto const* c) {
            auto x11_client = qobject_cast<Win const*>(c);
            return x11_client && x11_client->xcb_windows.input == w;
        }));
    }

    return nullptr;
}

}
