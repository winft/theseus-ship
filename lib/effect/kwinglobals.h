/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QPoint>
#include <QVariant>

#include <kwin_export.h>

#include <xcb/xcb.h>

#include <kwinconfig.h>

namespace KWin
{
KWIN_EXPORT Q_NAMESPACE

    enum CompositingType {
        NoCompositing = 0,
        OpenGLCompositing = 1,
        /*XRenderCompositing = 2,*/
        QPainterCompositing = 3,
    };

enum OpenGLPlatformInterface {
    NoOpenGLPlatformInterface = 0,
    GlxPlatformInterface,
    EglPlatformInterface
};

enum class OpenGLSafePoint { PreInit, PostInit, PreFrame, PostFrame, PostLastGuardedFrame };

enum clientAreaOption {
    PlacementArea,    // geometry where a window will be initially placed after being mapped
    MovementArea,     // ???  window movement snapping area?  ignore struts
    MaximizeArea,     // geometry to which a window will be maximized
    MaximizeFullArea, // like MaximizeArea, but ignore struts - used e.g. for topmenu
    FullScreenArea,   // area for fullscreen windows
    // these below don't depend on xinerama settings
    WorkArea,  // whole workarea (all screens together)
    FullArea,  // whole area (all screens together), ignore struts
    ScreenArea // one whole screen, ignore struts
};

}
