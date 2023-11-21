/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/setup_window.h>

namespace KWin::render::x11
{

template<typename Win>
void effect_setup_unmanaged_window_connections(Win& window)
{
    auto qtwin = window.qobject.get();
    auto eff_win = window.render->effect.get();

    QObject::connect(
        qtwin, &win::window_qobject::opacityChanged, eff_win, [&window, eff_win](auto old) {
            Q_EMIT eff_win->windowOpacityChanged(eff_win, old, window.opacity());
        });
    QObject::connect(
        qtwin, &win::window_qobject::frame_geometry_changed, eff_win, [eff_win](auto const& old) {
            eff_win->windowFrameGeometryChanged(eff_win, old);
        });
    QObject::connect(qtwin, &win::window_qobject::damaged, eff_win, [eff_win](auto const& rect) {
        eff_win->windowDamaged(eff_win, rect);
    });
    QObject::connect(qtwin, &win::window_qobject::visible_geometry_changed, eff_win, [eff_win]() {
        Q_EMIT eff_win->windowExpandedGeometryChanged(eff_win);
    });
}

}
