/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "effects.h"

#include "base/x11/platform.h"
#include "render/platform.h"

#include <KConfigGroup>
#include <memory>

namespace KWin::render::x11
{

class platform : public render::platform
{
public:
    platform(base::x11::platform& base)
        : render::platform(base)
    {
    }

    bool requiresCompositing() const override
    {
        return false;
    }

    bool openGLCompositingIsBroken() const override
    {
        const QString unsafeKey = QLatin1String("OpenGLIsUnsafe");
        return KConfigGroup(kwinApp()->config(), "Compositing").readEntry(unsafeKey, false);
    }

    std::unique_ptr<render::effects_handler_impl>
    createEffectsHandler(render::compositor* compositor, render::scene* scene) override
    {
        return std::make_unique<x11::effects_handler_impl>(compositor, scene);
    }
};

}
