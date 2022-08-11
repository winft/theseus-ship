/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "effect/update.h"

#include "base/wayland/server.h"
#include "main.h"
#include "render/window.h"
#include "toplevel.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

namespace KWin::render::wayland
{

using wayland_space = win::wayland::space<base::wayland::platform>;
using wayland_window = win::wayland::window<wayland_space>;

effects_handler_impl::effects_handler_impl(render::compositor* compositor, render::scene* scene)
    : render::effects_handler_impl(compositor, scene)
    , blur{*this, *waylandServer()->display}
    , contrast{*this, *waylandServer()->display}
    , slide{*this, *waylandServer()->display}
{
    reconfigure();

    auto space = compositor->space;

    // TODO(romangg): We do this for every window here, even for windows that are not an xdg-shell
    //                type window. Restrict that?
    QObject::connect(
        space->qobject.get(), &win::space::qobject_t::wayland_window_added, this, [this](auto c) {
            if (c->ready_for_painting) {
                slotXdgShellClientShown(c);
            } else {
                QObject::connect(c->qobject.get(),
                                 &Toplevel::qobject_t::windowShown,
                                 this,
                                 [this, c] { slotXdgShellClientShown(c); });
            }
        });

    // TODO(romangg): We do this here too for every window.
    for (auto window : space->windows) {
        auto wlwin = dynamic_cast<wayland_window*>(window);
        if (!wlwin) {
            continue;
        }
        if (wlwin->ready_for_painting) {
            setupAbstractClientConnections(wlwin);
        } else {
            QObject::connect(wlwin->qobject.get(),
                             &Toplevel::qobject_t::windowShown,
                             this,
                             [this, wlwin] { slotXdgShellClientShown(wlwin); });
        }
    }
}

effects_handler_impl::~effects_handler_impl()
{
    unloadAllEffects();
}

bool effects_handler_impl::eventFilter(QObject* watched, QEvent* event)
{
    handle_internal_window_effect_update_event(blur, watched, event);
    handle_internal_window_effect_update_event(contrast, watched, event);
    handle_internal_window_effect_update_event(slide, watched, event);
    return false;
}

EffectWindow* effects_handler_impl::find_window_by_surface(Wrapland::Server::Surface* surface) const
{
    if (auto win = static_cast<wayland_space*>(m_compositor->space)->find_window(surface)) {
        return win->render->effect.get();
    }
    return nullptr;
}

Wrapland::Server::Display* effects_handler_impl::waylandDisplay() const
{
    return waylandServer()->display.get();
}

effect::region_integration& effects_handler_impl::get_blur_integration()
{
    return blur;
}

effect::color_integration& effects_handler_impl::get_contrast_integration()
{
    return contrast;
}

effect::anim_integration& effects_handler_impl::get_slide_integration()
{
    return slide;
}

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
} kscreen_dummy;

effect::kscreen_integration& effects_handler_impl::get_kscreen_integration()
{
    return kscreen_dummy;
}

void effects_handler_impl::doStartMouseInterception(Qt::CursorShape shape)
{
    m_compositor->space->input->get_pointer()->setEffectsOverrideCursor(shape);
}

void effects_handler_impl::doStopMouseInterception()
{
    m_compositor->space->input->get_pointer()->removeEffectsOverrideCursor();
}

void effects_handler_impl::handle_effect_destroy(Effect& effect)
{
    unreserve_borders(effect);

    blur.remove(effect);
    contrast.remove(effect);
    slide.remove(effect);
}

}
