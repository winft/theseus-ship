/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"
#include "effect/slide_integration.h"

#include "render/effects.h"

namespace KWin::render::wayland
{

class KWIN_EXPORT effects_handler_impl : public render::effects_handler_impl
{
    Q_OBJECT
public:
    effects_handler_impl(render::compositor* compositor, render::scene* scene);
    ~effects_handler_impl();

    bool eventFilter(QObject* watched, QEvent* event) override;

    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* surface) const override;
    Wrapland::Server::Display* waylandDisplay() const override;

    effect::region_integration& get_blur_integration() override;
    effect::color_integration& get_contrast_integration() override;
    effect::anim_integration& get_slide_integration() override;
    effect::kscreen_integration& get_kscreen_integration() override;

    blur_integration<effects_handler_impl> blur;
    contrast_integration<effects_handler_impl> contrast;
    slide_integration<effects_handler_impl> slide;

protected:
    void handle_effect_destroy(Effect& effect) override;
};

}
