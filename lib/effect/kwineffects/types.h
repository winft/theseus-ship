/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QImage>
#include <QPair>
#include <QRect>
#include <climits>

namespace KWin
{

class Effect;
class EffectWindow;

typedef QPair<QString, Effect*> EffectPair;
typedef QList<EffectWindow*> EffectWindowList;

#define KWIN_EFFECT_API_MAKE_VERSION(major, minor) ((major) << 8 | (minor))
#define KWIN_EFFECT_API_VERSION_MAJOR 0
#define KWIN_EFFECT_API_VERSION_MINOR 233
#define KWIN_EFFECT_API_VERSION                                                                    \
    KWIN_EFFECT_API_MAKE_VERSION(KWIN_EFFECT_API_VERSION_MAJOR, KWIN_EFFECT_API_VERSION_MINOR)

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

}

}
