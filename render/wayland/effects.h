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

template<typename Compositor>
class effects_handler_impl : public render::effects_handler_impl<Compositor>
{
public:
    using space_t = typename Compositor::platform_t::space_t;

    effects_handler_impl(Compositor& compositor)
        : render::effects_handler_impl<Compositor>(compositor)
        , blur{*this, *waylandServer()->display}
        , contrast{*this, *waylandServer()->display}
        , slide{*this, *waylandServer()->display}
    {
        this->reconfigure();

        auto space = compositor.platform.base.space.get();

        // TODO(romangg): We do this for every window here, even for windows that are not an
        // xdg-shell
        //                type window. Restrict that?
        QObject::connect(
            space->qobject.get(),
            &win::space_qobject::wayland_window_added,
            this,
            [this](auto win_id) {
                auto win = this->compositor.platform.base.space->windows_map.at(win_id);
                if (win->ready_for_painting) {
                    this->slotXdgShellClientShown(win);
                } else {
                    QObject::connect(win->qobject.get(),
                                     &win::window_qobject::windowShown,
                                     this,
                                     [this, win] { this->slotXdgShellClientShown(win); });
                }
            });

        // TODO(romangg): We do this here too for every window.
        for (auto window : space->windows) {
            auto wlwin = dynamic_cast<typename space_t::wayland_window*>(window);
            if (!wlwin) {
                continue;
            }
            if (wlwin->ready_for_painting) {
                this->setupAbstractClientConnections(wlwin);
            } else {
                QObject::connect(wlwin->qobject.get(),
                                 &win::window_qobject::windowShown,
                                 this,
                                 [this, wlwin] { this->slotXdgShellClientShown(wlwin); });
            }
        }
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
        if (auto win = this->compositor.platform.base.space->find_window(surface)) {
            return win->render->effect.get();
        }
        return nullptr;
    }

    Wrapland::Server::Display* waylandDisplay() const override
    {
        return waylandServer()->display.get();
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

    blur_integration<effects_handler_impl> blur;
    contrast_integration<effects_handler_impl> contrast;
    slide_integration<effects_handler_impl> slide;

protected:
    void doStartMouseInterception(Qt::CursorShape shape) override
    {
        this->compositor.platform.base.space->input->pointer->setEffectsOverrideCursor(shape);
    }

    void doStopMouseInterception() override
    {
        this->compositor.platform.base.space->input->pointer->removeEffectsOverrideCursor();
    }

    void handle_effect_destroy(Effect& effect) override
    {
        this->unreserve_borders(effect);

        blur.remove(effect);
        contrast.remove(effect);
        slide.remove(effect);
    }

private:
    kscreen_integration kscreen_dummy;
};

}
