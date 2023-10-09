/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"
#include "effect/slide_integration.h"
#include <render/wayland/effect/xwayland.h>

#include "base/wayland/server.h"
#include "render/effects.h"
#include <render/wayland/setup_handler.h>
#include <win/wayland/space_windows.h>

namespace KWin::render::wayland
{

// KScreen effect is only available on X11.
class kscreen_integration : public effect::kscreen_integration
{
    void add(Effect& /*effect*/, update_function const& /*update*/) override
    {
    }
    void remove(Effect& /*effect*/) override
    {
    }
    void change_state(Effect& /*effect*/, double /*state*/) override
    {
    }
};

template<typename Scene>
class effects_handler_impl : public render::effects_handler_impl<Scene>
{
public:
    effects_handler_impl(Scene& scene)
        : render::effects_handler_impl<Scene>(scene)
        , blur{*this, *scene.compositor.platform.base.server->display}
        , contrast{*this, *scene.compositor.platform.base.server->display}
        , slide{*this, *scene.compositor.platform.base.server->display}
    {
        effect_setup_handler(*this);
    }

    ~effects_handler_impl()
    {
        this->unloadAllEffects();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        handle_internal_window_effect_update_event(blur, watched, event);
        handle_internal_window_effect_update_event(contrast, watched, event);
        handle_internal_window_effect_update_event(slide, watched, event);
        return false;
    }

    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* surface) const override
    {
        if (auto win = win::wayland::space_windows_find(*this->scene.compositor.platform.base.space,
                                                        surface)) {
            return win->render->effect.get();
        }
        return nullptr;
    }

    Wrapland::Server::Display* waylandDisplay() const override
    {
        return this->scene.compositor.platform.base.server->display.get();
    }

    effect::region_integration& get_blur_integration() override
    {
        return blur;
    }

    effect::color_integration& get_contrast_integration() override
    {
        return contrast;
    }

    effect::anim_integration& get_slide_integration() override
    {
        return slide;
    }

    effect::kscreen_integration& get_kscreen_integration() override
    {
        return kscreen_dummy;
    }

    blur_integration<effects_handler_impl, xwl_blur_support> blur;
    contrast_integration<effects_handler_impl, xwl_contrast_support> contrast;
    slide_integration<effects_handler_impl, xwl_slide_support> slide;

protected:
    void doStartMouseInterception(Qt::CursorShape shape) override
    {
        auto& space = this->scene.compositor.platform.base.space;
        space->input->pointer->setEffectsOverrideCursor(shape);
        if (auto& mov_res = space->move_resize_window) {
            std::visit(overload{[&](auto&& win) { win::end_move_resize(win); }}, *mov_res);
        }
    }

    void doStopMouseInterception() override
    {
        this->scene.compositor.platform.base.space->input->pointer->removeEffectsOverrideCursor();
    }

    void handle_effect_destroy(Effect& effect) override
    {
        this->unreserve_borders(effect);

        blur.remove(effect);
        contrast.remove(effect);
        slide.remove(effect);

        auto const properties = this->m_propertiesForEffects.keys();
        for (auto const& property : properties) {
            x11::remove_support_property(*this, &effect, property);
        }

        delete &effect;
    }

private:
    kscreen_integration kscreen_dummy;
};

}
