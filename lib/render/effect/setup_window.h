/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/window_qobject.h>

#include <QObject>

namespace KWin::effect
{

template<typename Win>
void setup_window_connections(Win& window)
{
    auto qtwin = window.qobject.get();
    auto eff_win = window.render->effect.get();

    QObject::connect(qtwin, &win::window_qobject::subspaces_changed, eff_win, [eff_win] {
        Q_EMIT eff_win->windowDesktopsChanged(eff_win);
    });
    QObject::connect(
        qtwin, &win::window_qobject::maximize_mode_changed, eff_win, [&window, eff_win](auto mode) {
            Q_EMIT eff_win->windowMaximizedStateChanged(
                eff_win,
                flags(mode & win::maximize_mode::horizontal),
                flags(mode & win::maximize_mode::vertical));
        });
    QObject::connect(qtwin, &win::window_qobject::clientStartUserMovedResized, eff_win, [eff_win] {
        Q_EMIT eff_win->windowStartUserMovedResized(eff_win);
    });
    QObject::connect(qtwin,
                     &win::window_qobject::clientStepUserMovedResized,
                     eff_win,
                     [eff_win](auto const& geometry) {
                         Q_EMIT eff_win->windowStepUserMovedResized(eff_win, geometry);
                     });
    QObject::connect(qtwin, &win::window_qobject::clientFinishUserMovedResized, eff_win, [eff_win] {
        Q_EMIT eff_win->windowFinishUserMovedResized(eff_win);
    });
    QObject::connect(
        qtwin, &win::window_qobject::opacityChanged, eff_win, [&window, eff_win](auto old) {
            Q_EMIT eff_win->windowOpacityChanged(eff_win, old, window.opacity());
        });
    QObject::connect(
        qtwin, &win::window_qobject::clientMinimized, eff_win, [eff_win](auto animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                Q_EMIT eff_win->minimizedChanged(eff_win);
            }
        });
    QObject::connect(
        qtwin, &win::window_qobject::clientUnminimized, eff_win, [eff_win](auto animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                Q_EMIT eff_win->minimizedChanged(eff_win);
            }
        });
    QObject::connect(qtwin, &win::window_qobject::modalChanged, eff_win, [eff_win] {
        eff_win->windowModalityChanged(eff_win);
    });
    QObject::connect(
        qtwin, &win::window_qobject::frame_geometry_changed, eff_win, [eff_win](auto const& old) {
            eff_win->windowFrameGeometryChanged(eff_win, old);
        });
    QObject::connect(qtwin, &win::window_qobject::damaged, eff_win, [eff_win](auto const& rect) {
        eff_win->windowDamaged(eff_win, rect);
    });
    QObject::connect(
        qtwin, &win::window_qobject::unresponsiveChanged, eff_win, [eff_win](bool unresponsive) {
            Q_EMIT eff_win->windowUnresponsiveChanged(eff_win, unresponsive);
        });
    QObject::connect(qtwin, &win::window_qobject::windowShown, eff_win, [eff_win] {
        Q_EMIT eff_win->windowShown(eff_win);
    });
    QObject::connect(qtwin, &win::window_qobject::windowHidden, eff_win, [eff_win] {
        Q_EMIT eff_win->windowHidden(eff_win);
    });
    QObject::connect(qtwin, &win::window_qobject::keepAboveChanged, eff_win, [eff_win](bool above) {
        Q_UNUSED(above)
        Q_EMIT eff_win->windowKeepAboveChanged(eff_win);
    });
    QObject::connect(qtwin, &win::window_qobject::keepBelowChanged, eff_win, [eff_win](bool below) {
        Q_UNUSED(below)
        Q_EMIT eff_win->windowKeepBelowChanged(eff_win);
    });
    QObject::connect(qtwin, &win::window_qobject::fullScreenChanged, eff_win, [eff_win]() {
        Q_EMIT eff_win->windowFullScreenChanged(eff_win);
    });
    QObject::connect(qtwin, &win::window_qobject::visible_geometry_changed, eff_win, [eff_win]() {
        Q_EMIT eff_win->windowExpandedGeometryChanged(eff_win);
    });
}

}
