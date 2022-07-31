/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "effects.h"

#include "base/wayland/platform.h"
#include "render/platform.h"

#include <memory>

namespace KWin::render::wayland
{

class platform : public render::platform
{
public:
    platform(base::wayland::platform& base)
        : render::platform(base)
    {
    }

    bool requiresCompositing() const override
    {
        return true;
    }

    bool compositingPossible() const override
    {
        return true;
    }

    QString compositingNotPossibleReason() const override
    {
        return {};
    }

    bool openGLCompositingIsBroken() const override
    {
        return false;
    }

    void createOpenGLSafePoint(OpenGLSafePoint /*safePoint*/) override
    {
    }

    outline_visual* create_non_composited_outline(render::outline* /*outline*/) override
    {
        return nullptr;
    }

    win::deco::renderer* createDecorationRenderer(win::deco::client_impl* client) override
    {
        if (!compositor->scene) {
            // TODO(romangg): Make this check unnecessary. No deco renderer should be created when
            //                there is no scene (fires in some integration tests).
            return nullptr;
        }
        return compositor->scene->createDecorationRenderer(client);
    }

    void invertScreen() override
    {
        assert(compositor->effects);
        compositor->effects->invert_screen();
    }

    std::unique_ptr<render::effects_handler_impl>
    createEffectsHandler(render::compositor* compositor, render::scene* scene) override
    {
        return std::make_unique<wayland::effects_handler_impl>(compositor, scene);
    }
};

}
