/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QImage>
#include <QMatrix4x4>
#include <QPair>
#include <QRect>
#include <climits>

#define KWIN_EFFECT_API_MAKE_VERSION(major, minor) ((major) << 8 | (minor))
#define KWIN_EFFECT_API_VERSION_MAJOR 0
#define KWIN_EFFECT_API_VERSION_MINOR 233
#define KWIN_EFFECT_API_VERSION                                                                    \
    KWIN_EFFECT_API_MAKE_VERSION(KWIN_EFFECT_API_VERSION_MAJOR, KWIN_EFFECT_API_VERSION_MINOR)

namespace KWin
{

class Effect;
class EffectWindow;

typedef QPair<QString, Effect*> EffectPair;

enum WindowQuadType {
    WindowQuadError, // for the stupid default ctor
    WindowQuadContents,
    WindowQuadDecoration,
    // Shadow Quad types
    WindowQuadShadow, // OpenGL only. The other shadow types are only used by Xrender
    WindowQuadShadowTop,
    WindowQuadShadowTopRight,
    WindowQuadShadowRight,
    WindowQuadShadowBottomRight,
    WindowQuadShadowBottom,
    WindowQuadShadowBottomLeft,
    WindowQuadShadowLeft,
    WindowQuadShadowTopLeft,
    EFFECT_QUAD_TYPE_START = 100 ///< @internal
};

/**
 * EffectWindow::setData() and EffectWindow::data() global roles.
 * All values between 0 and 999 are reserved for global roles.
 */
enum DataRole {
    // Grab roles are used to force all other animations to ignore the window.
    // The value of the data is set to the Effect's `this` value.
    WindowAddedGrabRole = 1,
    WindowClosedGrabRole,
    WindowMinimizedGrabRole,
    WindowUnminimizedGrabRole,
    WindowForceBlurRole,               ///< For fullscreen effects to enforce blurring of windows,
    WindowForceBackgroundContrastRole, ///< For fullscreen effects to enforce the background
                                       ///< contrast,
    LanczosCacheRole
};

/**
 * Style types used by @ref EffectFrame.
 * @since 4.6
 */
enum EffectFrameStyle {
    EffectFrameNone,     ///< Displays no frame around the contents.
    EffectFrameUnstyled, ///< Displays a basic box around the contents.
    EffectFrameStyled    ///< Displays a Plasma-styled frame around the contents.
};

enum KWinOption {
    CloseButtonCorner,
    SwitchDesktopOnScreenEdge,
    SwitchDesktopOnScreenEdgeMovingWindows,
};

/**
 * Represents the state of the session running outside kwin
 * Under Plasma this is managed by ksmserver
 */
enum class SessionState {
    Normal,
    Saving,
    Quitting,
};

/**
 * @brief The direction in which a pointer axis is moved.
 */
enum PointerAxisDirection {
    PointerAxisUp,
    PointerAxisDown,
    PointerAxisLeft,
    PointerAxisRight,
};

/**
 * @brief Directions for swipe gestures
 * @since 5.10
 */
enum class SwipeDirection {
    Invalid,
    Down,
    Left,
    Up,
    Right,
};

enum class PinchDirection {
    Expanding,
    Contracting,
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
    ElectricNone,
};

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

/**
 * Infinite region (i.e. a special region type saying that everything needs to be painted).
 */
inline QRect infiniteRegion()
{
    // INT_MIN / 2 because width/height is used (INT_MIN+INT_MAX==-1)
    return QRect(INT_MIN / 2, INT_MIN / 2, INT_MAX, INT_MAX);
}

// New types should go here.
namespace effect
{

// TODO(romangg): Replace with win::position once it has been ported to a common library.
enum class position {
    center = 0,
    left,
    top,
    right,
    bottom,
};

struct cursor_image {
    QImage image;
    QPoint hot_spot;
};

enum class transform_type {
    normal,
    rotated_90,
    rotated_180,
    rotated_270,
    flipped,
    flipped_90,
    flipped_180,
    flipped_270,
};

inline QMatrix4x4 get_transform_matrix(transform_type type)
{
    QMatrix4x4 matrix;

    auto flip = [&] { matrix.scale(-1, 1); };
    auto rot = [&](auto degree) { matrix.rotate(degree, 0, 0, 1); };

    switch (type) {
    case transform_type::normal:
        break;
    case transform_type::flipped:
        flip();
        break;
    case transform_type::flipped_90:
        flip();
    case transform_type::rotated_90:
        rot(90);
        break;
    case transform_type::flipped_180:
        flip();
    case transform_type::rotated_180:
        rot(180);
        break;
    case transform_type::flipped_270:
        flip();
    case transform_type::rotated_270:
        rot(270);
        break;
    }

    return matrix;
}

}
}
