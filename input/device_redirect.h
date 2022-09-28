/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "win/deco/client_impl.h"

#include <QWindow>

namespace KWin::input
{

/**
 * Element currently at the position of the input device according to the stacking order. Might be
 * null if no element is at the position.
 */
template<typename Window>
struct device_redirect_at {
    Window* window{nullptr};
    struct {
        QMetaObject::Connection surface;
        QMetaObject::Connection destroy;
    } notifiers;
};

/**
 * Element currently having device input focus (this might be different from the window at the
 * position of the device). Might be null if no element has focus.
 */
template<typename Window>
struct device_redirect_focus {
    Window* window{nullptr};
    win::deco::client_impl<Window>* deco{nullptr};
    QWindow* internal_window{nullptr};
    struct {
        QMetaObject::Connection window_destroy;
        QMetaObject::Connection deco_destroy;
        QMetaObject::Connection internal_window_destroy;
    } notifiers;
};

}
