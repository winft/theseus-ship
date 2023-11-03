/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/window_qobject.h>

#include <QObject>

namespace KWin::effect
{

template<typename Handler, typename Win>
void setup_handler_window_connections(Handler& handler, Win& window)
{
    auto qtwin = window.qobject.get();

    QObject::connect(qtwin, &win::window_qobject::subspaces_changed, &handler, [&handler, &window] {
        Q_EMIT handler.windowDesktopsChanged(window.render->effect.get());
    });
    QObject::connect(qtwin,
                     &win::window_qobject::maximize_mode_changed,
                     &handler,
                     [&handler, &window](auto mode) { handler.slotClientMaximized(window, mode); });
    QObject::connect(
        qtwin, &win::window_qobject::clientStartUserMovedResized, &handler, [&handler, &window] {
            Q_EMIT handler.windowStartUserMovedResized(window.render->effect.get());
        });
    QObject::connect(qtwin,
                     &win::window_qobject::clientStepUserMovedResized,
                     &handler,
                     [&handler, &window](QRect const& geometry) {
                         Q_EMIT handler.windowStepUserMovedResized(window.render->effect.get(),
                                                                   geometry);
                     });
    QObject::connect(
        qtwin, &win::window_qobject::clientFinishUserMovedResized, &handler, [&handler, &window] {
            Q_EMIT handler.windowFinishUserMovedResized(window.render->effect.get());
        });
    QObject::connect(qtwin,
                     &win::window_qobject::opacityChanged,
                     &handler,
                     [&handler, &window](auto old) { handler.slotOpacityChanged(window, old); });
    QObject::connect(
        qtwin, &win::window_qobject::clientMinimized, &handler, [&handler, &window](auto animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                Q_EMIT handler.windowMinimized(window.render->effect.get());
            }
        });
    QObject::connect(qtwin,
                     &win::window_qobject::clientUnminimized,
                     &handler,
                     [&handler, &window](auto animate) {
                         // TODO: notify effects even if it should not animate?
                         if (animate) {
                             Q_EMIT handler.windowUnminimized(window.render->effect.get());
                         }
                     });
    QObject::connect(qtwin, &win::window_qobject::modalChanged, &handler, [&handler, &window] {
        handler.slotClientModalityChanged(window);
    });
    QObject::connect(
        qtwin,
        &win::window_qobject::frame_geometry_changed,
        &handler,
        [&handler, &window](auto const& rect) { handler.slotFrameGeometryChanged(window, rect); });
    QObject::connect(
        qtwin, &win::window_qobject::damaged, &handler, [&handler, &window](auto const& rect) {
            handler.slotWindowDamaged(window, rect);
        });
    QObject::connect(qtwin,
                     &win::window_qobject::unresponsiveChanged,
                     &handler,
                     [&handler, &window](bool unresponsive) {
                         Q_EMIT handler.windowUnresponsiveChanged(window.render->effect.get(),
                                                                  unresponsive);
                     });
    QObject::connect(qtwin, &win::window_qobject::windowShown, &handler, [&handler, &window] {
        Q_EMIT handler.windowShown(window.render->effect.get());
    });
    QObject::connect(qtwin, &win::window_qobject::windowHidden, &handler, [&handler, &window] {
        Q_EMIT handler.windowHidden(window.render->effect.get());
    });
    QObject::connect(
        qtwin, &win::window_qobject::keepAboveChanged, &handler, [&handler, &window](bool above) {
            Q_UNUSED(above)
            Q_EMIT handler.windowKeepAboveChanged(window.render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::keepBelowChanged, &handler, [&handler, &window](bool below) {
            Q_UNUSED(below)
            Q_EMIT handler.windowKeepBelowChanged(window.render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::fullScreenChanged, &handler, [&handler, &window]() {
            Q_EMIT handler.windowFullScreenChanged(window.render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::visible_geometry_changed, &handler, [&handler, &window]() {
            Q_EMIT handler.windowExpandedGeometryChanged(window.render->effect.get());
        });
}

}
