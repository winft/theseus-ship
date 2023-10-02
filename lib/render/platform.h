/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "outline.h"

namespace KWin::render
{

class platform
{
public:
    // TODO(romangg): For unknown reason this using declaration can't be set in the child classes.
    //                Maybe because there is a Q_OBJECT macro in render::outline?
    using outline_t = render::outline;

    virtual ~platform() = default;

    // TODO(romangg): Remove the boolean trap.
    virtual void render_stop(bool on_shutdown) = 0;

    virtual bool requiresCompositing() const = 0;
    virtual bool compositingPossible() const = 0;
    virtual QString compositingNotPossibleReason() const = 0;
    virtual bool openGLCompositingIsBroken() const = 0;
    virtual void createOpenGLSafePoint(opengl_safe_point safePoint) = 0;

    /**
     * Platform specific way to invert the screen.
     * Default implementation invokes the invert effect
     */
    virtual void invertScreen() = 0;

    virtual bool is_sw_compositing() const = 0;
};

}
