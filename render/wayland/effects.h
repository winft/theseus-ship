/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../../effects.h"

namespace KWin::render::wayland
{

class KWIN_EXPORT effects_handler_impl : public EffectsHandlerImpl
{
    Q_OBJECT
public:
    effects_handler_impl(render::compositor* compositor, Scene* scene);

    EffectWindow* findWindow(Wrapland::Server::Surface* surf) const override;
    Wrapland::Server::Display* waylandDisplay() const override;
};

}
