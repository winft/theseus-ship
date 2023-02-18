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

#define KWIN_QT5_PORTING 0

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

enum ElectricBorder {
    ElectricTop,
    ElectricTopRight,
    ElectricRight,
    ElectricBottomRight,
    ElectricBottom,
    ElectricBottomLeft,
    ElectricLeft,
    ElectricTopLeft,
    ELECTRIC_COUNT,
    ElectricNone
};
Q_ENUM_NS(ElectricBorder)

// TODO: Hardcoding is bad, need to add some way of registering global actions to these.
// When designing the new system we must keep in mind that we have conditional actions
// such as "only when moving windows" desktop switching that the current global action
// system doesn't support.
enum ElectricBorderAction {
    ElectricActionNone,                // No special action, not set, desktop switch or an effect
    ElectricActionShowDesktop,         // Show desktop or restore
    ElectricActionLockScreen,          // Lock screen
    ElectricActionKRunner,             // Open KRunner
    ElectricActionApplicationLauncher, // Application Launcher
    ELECTRIC_ACTION_COUNT
};

// DesktopMode and WindowsMode are based on the order in which the desktop
//  or window were viewed.
// DesktopListMode lists them in the order created.
enum TabBoxMode {
    TabBoxDesktopMode,            // Focus chain of desktops
    TabBoxDesktopListMode,        // Static desktop order
    TabBoxWindowsMode,            // Primary window switching mode
    TabBoxWindowsAlternativeMode, // Secondary window switching mode
    TabBoxCurrentAppWindowsMode,  // Same as primary window switching mode but only for windows of
                                  // current application
    TabBoxCurrentAppWindowsAlternativeMode // Same as secondary switching mode but only for windows
                                           // of current application
};

enum KWinOption {
    CloseButtonCorner,
    SwitchDesktopOnScreenEdge,
    SwitchDesktopOnScreenEdgeMovingWindows
};

/**
 * @brief The direction in which a pointer axis is moved.
 */
enum PointerAxisDirection { PointerAxisUp, PointerAxisDown, PointerAxisLeft, PointerAxisRight };

/**
 * @brief Directions for swipe gestures
 * @since 5.10
 */
enum class SwipeDirection { Invalid, Down, Left, Up, Right };

enum class PinchDirection { Expanding, Contracting };

enum class DeviceType { Touchpad, Touchscreen };

/**
 * Represents the state of the session running outside kwin
 * Under Plasma this is managed by ksmserver
 */
enum class SessionState { Normal, Saving, Quitting };
Q_ENUM_NS(SessionState)

/**
 * Short wrapper for a cursor image provided by the Platform.
 * @since 5.9
 */
class PlatformCursorImage
{
public:
    explicit PlatformCursorImage()
        : m_image()
        , m_hotSpot()
    {
    }
    explicit PlatformCursorImage(const QImage& image, const QPoint& hotSpot)
        : m_image(image)
        , m_hotSpot(hotSpot)
    {
    }
    virtual ~PlatformCursorImage() = default;

    QImage image() const
    {
        return m_image;
    }
    QPoint hotSpot() const
    {
        return m_hotSpot;
    }

private:
    QImage m_image;
    QPoint m_hotSpot;
};

}
