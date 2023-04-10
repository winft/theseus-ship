/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "property_window.h"

#include "window_qobject.h"

namespace KWin::win
{

property_window::property_window(window_qobject& qtwin)
    : qtwin{qtwin}
{
    setup_connections();
}

void property_window::setup_connections()
{
    QObject::connect(&qtwin, &win::window_qobject::opacityChanged, this, [this](auto old_opacity) {
        Q_EMIT opacityChanged(this, old_opacity);
    });

    QObject::connect(
        &qtwin, &win::window_qobject::activeChanged, this, &property_window::activeChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::demandsAttentionChanged,
                     this,
                     &property_window::demandsAttentionChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::desktopsChanged, this, &property_window::desktopsChanged);

    QObject::connect(
        &qtwin, &win::window_qobject::minimizedChanged, this, &property_window::minimizedChanged);

    QObject::connect(
        &qtwin, &win::window_qobject::keepAboveChanged, this, &property_window::keepAboveChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::keepBelowChanged, this, &property_window::keepBelowChanged);

    QObject::connect(
        &qtwin, &win::window_qobject::fullScreenChanged, this, &property_window::fullScreenChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::skipTaskbarChanged,
                     this,
                     &property_window::skipTaskbarChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::skipPagerChanged, this, &property_window::skipPagerChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::skipSwitcherChanged,
                     this,
                     &property_window::skipSwitcherChanged);

    QObject::connect(&qtwin,
                     &win::window_qobject::colorSchemeChanged,
                     this,
                     &property_window::colorSchemeChanged);

    // TODO(romangg): Is this problematic for scripts that connect to the overriding
    // transientChanged signal?
    QObject::connect(
        &qtwin, &win::window_qobject::transientChanged, this, &property_window::transientChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::modalChanged, this, &property_window::modalChanged);

    QObject::connect(&qtwin,
                     &win::window_qobject::moveResizedChanged,
                     this,
                     &property_window::moveResizedChanged);

    QObject::connect(&qtwin,
                     &win::window_qobject::windowClassChanged,
                     this,
                     &property_window::windowClassChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::captionChanged, this, &property_window::captionChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::iconChanged, this, &property_window::iconChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::frame_geometry_changed,
                     this,
                     &property_window::geometryChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::hasAlphaChanged, this, &property_window::hasAlphaChanged);
    QObject::connect(
        &qtwin, &win::window_qobject::windowRoleChanged, this, &property_window::windowRoleChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::skipCloseAnimationChanged,
                     this,
                     &property_window::skipCloseAnimationChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::applicationMenuActiveChanged,
                     this,
                     &property_window::applicationMenuActiveChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::unresponsiveChanged,
                     this,
                     &property_window::unresponsiveChanged);
    QObject::connect(&qtwin,
                     &win::window_qobject::hasApplicationMenuChanged,
                     this,
                     &property_window::hasApplicationMenuChanged);

    QObject::connect(&qtwin,
                     &win::window_qobject::desktopFileNameChanged,
                     this,
                     &property_window::desktopFileNameChanged);
}

window_qobject* property_window::get_window_qobject()
{
    return &qtwin;
}

}
