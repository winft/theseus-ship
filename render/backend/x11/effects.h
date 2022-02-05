/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/window.h"
#include "render/effects.h"

#include <memory.h>

namespace KWin::render::backend::x11
{

class mouse_intercept_filter;

class effects_handler_impl : public render::effects_handler_impl
{
    Q_OBJECT
public:
    effects_handler_impl(render::compositor* compositor, render::scene* scene);
    ~effects_handler_impl() override;

    void defineCursor(Qt::CursorShape shape) override;

protected:
    bool doGrabKeyboard() override;
    void doUngrabKeyboard() override;

    void doStartMouseInterception(Qt::CursorShape shape) override;
    void doStopMouseInterception() override;

    void doCheckInputWindowStacking() override;

private:
    struct {
        base::x11::xcb::window window;
        std::unique_ptr<mouse_intercept_filter> filter;
    } mouse_intercept;
};

}
