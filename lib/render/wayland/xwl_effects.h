/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"
#include "effect/slide_integration.h"
#include <render/wayland/effect/xwayland.h>
#include <render/wayland/effects.h>

#include "base/wayland/server.h"
#include "render/effects.h"
#include <render/wayland/setup_handler.h>
#include <render/x11/effect/setup_handler.h>
#include <render/x11/effect/setup_window.h>
#include <win/wayland/space_windows.h>

namespace KWin::render::wayland
{

template<typename Scene>
class xwl_effects_handler_impl : public render::effects_handler_impl<Scene>
{
public:
    using type = xwl_effects_handler_impl<Scene>;
    using abstract_type = render::effects_handler_impl<Scene>;

    xwl_effects_handler_impl(Scene& scene)
        : abstract_type(scene)
        , blur{*this, *scene.platform.base.server->display}
        , contrast{*this, *scene.platform.base.server->display}
        , slide{*this, *scene.platform.base.server->display}
    {
        effect::setup_handler(*this);
        x11::effect_setup_handler(*this);
        effect_setup_handler(*this);
    }

    ~xwl_effects_handler_impl()
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
            = win::wayland::space_windows_find(*this->scene.platform.base.mod.space, surface)) {
            return win->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_wid(WId id) const override
    {
        return x11::find_window_by_wid(this->get_space(), id);
    }

    Wrapland::Server::Display* waylandDisplay() const override
    {
        return this->scene.platform.base.server->display.get();
    }

    xcb_connection_t* xcbConnection() const override
    {
        return this->scene.platform.base.x11_data.connection;
    }

    xcb_window_t x11RootWindow() const override
    {
        return this->scene.platform.base.x11_data.root_window;
    }

    SessionState sessionState() const override
    {
        return static_cast<SessionState>(this->get_space().session_manager->state());
    }

    QByteArray readRootProperty(long atom, long type, int format) const override
    {
        return x11::read_root_property(this->scene.platform.base, atom, type, format);
    }

    template<typename Win>
    void slotUnmanagedShown(Win& window)
    { // regardless, unmanaged windows are -yet?- not synced anyway
        assert(!window.control);
        x11::effect_setup_unmanaged_window_connections(window);
        Q_EMIT this->windowAdded(window.render->effect.get());
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

    blur_integration<xwl_effects_handler_impl, xwl_blur_support> blur;
    contrast_integration<xwl_effects_handler_impl, xwl_contrast_support> contrast;
    slide_integration<xwl_effects_handler_impl, xwl_slide_support> slide;

    std::unique_ptr<x11::property_notify_filter<type, typename abstract_type::space_t>>
        x11_property_notify;

protected:
    void doStartMouseInterception(Qt::CursorShape shape) override
    {
        auto& space = this->scene.platform.base.mod.space;
        space->input->pointer->setEffectsOverrideCursor(shape);
        if (auto& mov_res = space->move_resize_window) {
            std::visit(overload{[&](auto&& win) { win::end_move_resize(win); }}, *mov_res);
        }
    }

    void doStopMouseInterception() override
    {
        this->scene.platform.base.mod.space->input->pointer->removeEffectsOverrideCursor();
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
