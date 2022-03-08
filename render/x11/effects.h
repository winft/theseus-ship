/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"

#include "base/x11/xcb/window.h"
#include "render/effects.h"

#include <memory.h>

namespace KWin::render::x11
{

class mouse_intercept_filter;

class effects_handler_impl : public render::effects_handler_impl
{
    Q_OBJECT
public:
    effects_handler_impl(render::compositor* compositor, render::scene* scene);
    ~effects_handler_impl() override;

    bool eventFilter(QObject* watched, QEvent* event) override;

    void defineCursor(Qt::CursorShape shape) override;
    QImage blit_from_framebuffer(QRect const& geometry, double scale) const override;

    effect::region_integration& get_blur_integration() override;
    effect::color_integration& get_contrast_integration() override;

    blur_integration<effects_handler_impl> blur;
    contrast_integration<effects_handler_impl> contrast;

protected:
    bool doGrabKeyboard() override;
    void doUngrabKeyboard() override;

    void doStartMouseInterception(Qt::CursorShape shape) override;
    void doStopMouseInterception() override;

    void doCheckInputWindowStacking() override;
    void handle_effect_destroy(Effect& effect) override;

private:
    struct {
        base::x11::xcb::window window;
        std::unique_ptr<mouse_intercept_filter> filter;
    } mouse_intercept;
};

}
