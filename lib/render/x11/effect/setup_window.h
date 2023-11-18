/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/setup_window.h>

namespace KWin::render::x11
{

template<typename Handler, typename Win>
void effect_setup_unmanaged_window_connections(Handler& handler, Win& window)
{
    QObject::connect(window.qobject.get(),
                     &win::window_qobject::opacityChanged,
                     &handler,
                     [&handler, &window](auto old) { handler.slotOpacityChanged(window, old); });
    QObject::connect(
        window.qobject.get(),
        &win::window_qobject::frame_geometry_changed,
        &handler,
        [&handler, &window](auto const& old) { handler.slotFrameGeometryChanged(window, old); });
    QObject::connect(
        window.qobject.get(),
        &win::window_qobject::damaged,
        &handler,
        [&handler, &window](auto const& region) { handler.slotWindowDamaged(window, region); });
    QObject::connect(window.qobject.get(),
                     &win::window_qobject::visible_geometry_changed,
                     &handler,
                     [&handler, &window]() {
                         Q_EMIT handler.windowExpandedGeometryChanged(window.render->effect.get());
                     });
}

}
