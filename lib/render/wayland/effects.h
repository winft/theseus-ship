/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"
#include "effect/slide_integration.h"

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
    using type = effects_handler_impl<Scene>;
    using abstract_type = render::effects_handler_impl<Scene>;

    effects_handler_impl(Scene& scene)
        : abstract_type(scene)
        , blur{*this, *scene.platform.base.server->display}
        , contrast{*this, *scene.platform.base.server->display}
        , slide{*this, *scene.platform.base.server->display}
    {
        effect::setup_handler(*this);
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
        if (auto win
            = win::wayland::space_windows_find(*this->scene.platform.base.space, surface)) {
            return win->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_wid(WId /*id*/) const override
    {
        return nullptr;
    }

    Wrapland::Server::Display* waylandDisplay() const override
    {
        return this->scene.platform.base.server->display.get();
    }

    xcb_connection_t* xcbConnection() const override
    {
        return nullptr;
    }

    uint32_t x11RootWindow() const override
    {
        return 0;
    }

    SessionState sessionState() const override
    {
        return SessionState::Normal;
    }

    QByteArray readRootProperty(long /*atom*/, long /*type*/, int /*format*/) const override
    {
        return {};
    }

    template<typename Win>
    void slotUnmanagedShown(Win& /*window*/)
    {
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

    blur_integration<effects_handler_impl, blur_support> blur;
    contrast_integration<effects_handler_impl, contrast_support> contrast;
    slide_integration<effects_handler_impl, slide_support> slide;

protected:
    void doStartMouseInterception(Qt::CursorShape shape) override
    {
        auto& space = this->scene.platform.base.space;
        space->input->pointer->setEffectsOverrideCursor(shape);
        if (auto& mov_res = space->move_resize_window) {
            std::visit(overload{[&](auto&& win) { win::end_move_resize(win); }}, *mov_res);
        }
    }

    void doStopMouseInterception() override
    {
        this->scene.platform.base.space->input->pointer->removeEffectsOverrideCursor();
    }

    void handle_effect_destroy(Effect& effect) override
    {
        this->unreserve_borders(effect);

        blur.remove(effect);
        contrast.remove(effect);
        slide.remove(effect);

        delete &effect;
    }

private:
    kscreen_integration kscreen_dummy;
};

}
