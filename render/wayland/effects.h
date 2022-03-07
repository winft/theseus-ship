/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/blur_integration.h"

#include "render/effects.h"

namespace KWin::render::wayland
{

class KWIN_EXPORT effects_handler_impl : public render::effects_handler_impl
{
    Q_OBJECT
public:
    effects_handler_impl(render::compositor* compositor, render::scene* scene);

    bool eventFilter(QObject* watched, QEvent* event) override;

    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* surface) const override;
    Wrapland::Server::Display* waylandDisplay() const override;

    effect::region_integration& get_blur_integration() override;

    blur_integration<effects_handler_impl> blur;

protected:
    void handle_effect_destroy(Effect& effect) override;
};

}
