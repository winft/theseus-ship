/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <QFlags>
#include <kwinglobals.h>

namespace KWin::win::x11::net
{

enum Property {
    // root
    Supported = 1u << 0,
    ClientList = 1u << 1,
    ClientListStacking = 1u << 2,
    NumberOfDesktops = 1u << 3,
    DesktopGeometry = 1u << 4,
    DesktopViewport = 1u << 5,
    CurrentDesktop = 1u << 6,
    DesktopNames = 1u << 7,
    ActiveWindow = 1u << 8,
    WorkArea = 1u << 9,
    SupportingWMCheck = 1u << 10,
    VirtualRoots = 1u << 11,
    //
    CloseWindow = 1u << 13,
    WMMoveResize = 1u << 14,

    // window
    WMName = 1u << 15,
    WMVisibleName = 1u << 16,
    WMDesktop = 1u << 17,
    WMWindowType = 1u << 18,
    WMState = 1u << 19,
    WMStrut = 1u << 20,
    WMIconGeometry = 1u << 21,
    WMIcon = 1u << 22,
    WMPid = 1u << 23,
    WMHandledIcons = 1u << 24,
    WMPing = 1u << 25,
    XAWMState = 1u << 27,
    WMFrameExtents = 1u << 28,

    // Need to be reordered
    WMIconName = 1u << 29,
    WMVisibleIconName = 1u << 30,
    WMGeometry = 1u << 31,
    WMAllProperties = ~0u,
};
Q_DECLARE_FLAGS(Properties, Property)

/// This enum is an extension to Property, because the enum is limited only to 32 bits.
enum Property2 {
    WM2UserTime = 1u << 0,
    WM2StartupId = 1u << 1,
    WM2TransientFor = 1u << 2,
    WM2GroupLeader = 1u << 3,
    WM2AllowedActions = 1u << 4,
    WM2RestackWindow = 1u << 5,
    WM2MoveResizeWindow = 1u << 6,
    WM2ExtendedStrut = 1u << 7,
    WM2KDETemporaryRules = 1u << 8, // NOT STANDARD
    WM2WindowClass = 1u << 9,
    WM2WindowRole = 1u << 10,
    WM2ClientMachine = 1u << 11,
    WM2ShowingDesktop = 1u << 12,
    WM2Opacity = 1u << 13,
    WM2DesktopLayout = 1u << 14,
    WM2FullPlacement = 1u << 15,
    WM2FullscreenMonitors = 1u << 16,
    WM2FrameOverlap = 1u << 17, // NOT STANDARD
    WM2Activities = 1u << 18,   // NOT STANDARD
    WM2BlockCompositing = 1u << 19,
    WM2KDEShadow = 1u << 20, // NOT Standard
    WM2Urgency = 1u << 21,
    WM2Input = 1u << 22,
    WM2Protocols = 1u << 23,
    WM2InitialMappingState = 1u << 24,
    WM2IconPixmap = 1u << 25,
    WM2OpaqueRegion = 1u << 25,
    WM2DesktopFileName = 1u << 26,    // NOT STANDARD
    WM2GTKFrameExtents = 1u << 27,    // NOT STANDARD
    WM2AppMenuServiceName = 1u << 28, // NOT STANDARD
    WM2AppMenuObjectPath = 1u << 29,  // NOT STANDARD
    WM2GTKApplicationId = 1u << 30,   // NOT STANDARD
    WM2GTKShowWindowMenu = 1u << 31,  // NOT STANDARD
    WM2AllProperties = ~0u,
};
Q_DECLARE_FLAGS(Properties2, Property2)

enum {
    OnAllDesktops = -1,
};

enum RequestSource {
    // internal
    FromUnknown = 0,
    FromApplication = 1,
    FromTool = 2,
};

enum Orientation {
    OrientationHorizontal = 0,
    OrientationVertical = 1,
};

enum DesktopLayoutCorner {
    DesktopLayoutCornerTopLeft = 0,
    DesktopLayoutCornerTopRight = 1,
    DesktopLayoutCornerBottomLeft = 2,
    DesktopLayoutCornerBottomRight = 3,
};

enum Protocol {
    NoProtocol = 0,
    TakeFocusProtocol = 1 << 0,    ///< WM_TAKE_FOCUS
    DeleteWindowProtocol = 1 << 1, ///< WM_DELETE_WINDOW
    PingProtocol = 1 << 2,         ///< _NET_WM_PING from EWMH
    SyncRequestProtocol = 1 << 3,  ///< _NET_WM_SYNC_REQUEST from EWMH
    ContextHelpProtocol = 1 << 4,  ///< _NET_WM_CONTEXT_HELP, NON STANDARD!
};
Q_DECLARE_FLAGS(Protocols, Protocol)

enum Role {
    Client,
    WindowManager,
};

enum State {
    Modal = 1u << 0,
    Sticky = 1u << 1,
    MaxVert = 1u << 2,
    MaxHoriz = 1u << 3,
    Max = MaxVert | MaxHoriz,
    Shaded = 1u << 4,
    SkipTaskbar = 1u << 5,
    KeepAbove = 1u << 6,
    SkipPager = 1u << 7,
    Hidden = 1u << 8,
    FullScreen = 1u << 9,
    KeepBelow = 1u << 10,
    DemandsAttention = 1u << 11,
    SkipSwitcher = 1u << 12,
    Focused = 1u << 13,
};
Q_DECLARE_FLAGS(States, State)

enum Direction {
    TopLeft = 0,
    Top = 1,
    TopRight = 2,
    Right = 3,
    BottomRight = 4,
    Bottom = 5,
    BottomLeft = 6,
    Left = 7,
    Move = 8,              // movement only
    KeyboardSize = 9,      // size via keyboard
    KeyboardMove = 10,     // move via keyboard
    MoveResizeCancel = 11, // to ask the WM to stop moving a window
};

enum MappingState {
    Visible = 1,   // NormalState,
    Withdrawn = 0, // WithdrawnState,
    Iconic = 3,    // IconicState
};

enum Action {
    ActionMove = 1u << 0,
    ActionResize = 1u << 1,
    ActionMinimize = 1u << 2,
    ActionShade = 1u << 3,
    ActionStick = 1u << 4,
    ActionMaxVert = 1u << 5,
    ActionMaxHoriz = 1u << 6,
    ActionMax = ActionMaxVert | ActionMaxHoriz,
    ActionFullScreen = 1u << 7,
    ActionChangeDesktop = 1u << 8,
    ActionClose = 1u << 9,
};
Q_DECLARE_FLAGS(Actions, Action)

KWIN_EXPORT bool typeMatchesMask(win::win_type type, win::window_type_mask mask);

/**
 Compares two X timestamps, taking into account wrapping and 64bit architectures.
 Return value is like with strcmp(), 0 for equal, -1 for time1 < time2, 1 for time1 > time2.
*/
KWIN_EXPORT int timestampCompare(unsigned long time1_, unsigned long time2_);

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::win::x11::net::Properties)
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::win::x11::net::Properties2)
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::win::x11::net::States)
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::win::x11::net::Actions)
Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::win::x11::net::Protocols)
