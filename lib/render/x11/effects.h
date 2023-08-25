/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "keyboard_intercept_filter.h"
#include "mouse_intercept_filter.h"

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"
#include "effect/kscreen_integration.h"
#include "effect/slide_integration.h"

#include "base/x11/xcb/window.h"
#include "render/effects.h"
#include "render/xrender/utils.h"
#include "win/x11/space.h"

#include <memory.h>

namespace KWin::render::x11
{

template<typename Scene>
class effects_handler_impl : public render::effects_handler_impl<Scene>
{
public:
    using type = effects_handler_impl<Scene>;

    effects_handler_impl(Scene& scene)
        : render::effects_handler_impl<Scene>(scene)
        , blur{*this}
        , contrast{*this}
        , slide{*this}
        , kscreen{*this}
    {
        this->reconfigure();

        QObject::connect(
            this, &effects_handler_impl::screenGeometryChanged, this, [this](const QSize& size) {
                if (mouse_intercept.window.is_valid()) {
                    mouse_intercept.window.set_geometry(QRect(0, 0, size.width(), size.height()));
                }
            });
    }

    ~effects_handler_impl() override
    {
        // EffectsHandlerImpl tries to unload all effects when it's destroyed.
        // The routine that unloads effects makes some calls (indirectly) to
        // doUngrabKeyboard and doStopMouseInterception, which are virtual.
        // Given that any call to a virtual function in the destructor of a base
        // class will never go to a derived class, we have to unload effects
        // here. Yeah, this is quite a bit ugly but it's fine; someday, X11
        // will be dead (or not?).
        this->unloadAllEffects();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        handle_internal_window_effect_update_event(blur, watched, event);
        handle_internal_window_effect_update_event(contrast, watched, event);
        handle_internal_window_effect_update_event(slide, watched, event);
        return false;
    }

    void defineCursor(Qt::CursorShape shape) override
    {
        auto const c = this->scene.platform.base.space->input->cursor->x11_cursor(shape);
        if (c != XCB_CURSOR_NONE) {
            mouse_intercept.window.define_cursor(c);
        }
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
        return kscreen;
    }

    blur_integration<effects_handler_impl> blur;
    contrast_integration<effects_handler_impl> contrast;
    slide_integration<effects_handler_impl> slide;
    kscreen_integration<effects_handler_impl> kscreen;

protected:
    bool doGrabKeyboard() override
    {
        if (!this->scene.platform.base.input->grab_keyboard()) {
            return false;
        }

        auto xkb = this->scene.platform.base.space->input->xinput->fake_devices.keyboard->xkb.get();
        using xkb_keyboard_t = std::remove_pointer_t<decltype(xkb)>;
        keyboard_intercept.filter
            = std::make_unique<keyboard_intercept_filter<type, xkb_keyboard_t>>(*this, *xkb);

        return true;
    }

    void doUngrabKeyboard() override
    {
        this->scene.platform.base.input->ungrab_keyboard();
        keyboard_intercept.filter.reset();
    }

    void doStartMouseInterception(Qt::CursorShape shape) override
    {
        // NOTE: it is intended to not perform an XPointerGrab on X11. See documentation in
        // kwineffects.h The mouse grab is implemented by using a full screen input only window
        if (!mouse_intercept.window.is_valid()) {
            auto const& base = this->scene.platform.base;
            auto const& x11_data = base.x11_data;
            auto const& space_size = base.topology.size;
            const QRect geo(0, 0, space_size.width(), space_size.height());
            const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
            const uint32_t values[] = {true,
                                       XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                           | XCB_EVENT_MASK_POINTER_MOTION};
            auto xcb_win = base::x11::xcb::create_input_window(
                x11_data.connection, x11_data.root_window, geo, mask, values);
            mouse_intercept.window.reset(x11_data.connection, xcb_win);
            defineCursor(shape);
        } else {
            defineCursor(shape);
        }

        mouse_intercept.window.map();
        mouse_intercept.window.raise();
        mouse_intercept.filter
            = std::make_unique<mouse_intercept_filter<type>>(mouse_intercept.window, this);

        // Raise electric border windows above the input windows
        // so they can still be triggered.
        this->scene.platform.base.space->edges->ensureOnTop();
    }

    void doStopMouseInterception() override
    {
        mouse_intercept.window.unmap();
        mouse_intercept.filter.reset();
        win::x11::stack_screen_edges_under_override_redirect(this->scene.platform.base.space.get());
    }

    void doCheckInputWindowStacking() override
    {
        mouse_intercept.window.raise();

        // Raise electric border windows above the input windows
        // so they can still be triggered. TODO: Do both at once.
        this->scene.platform.base.space->edges->ensureOnTop();
    }

    void handle_effect_destroy(Effect& effect) override
    {
        this->unreserve_borders(effect);

        blur.remove(effect);
        contrast.remove(effect);
        slide.remove(effect);
        kscreen.remove(effect);
    }

private:
    struct {
        base::x11::xcb::window window;
        std::unique_ptr<mouse_intercept_filter<type>> filter;
    } mouse_intercept;
    struct {
        base::x11::xcb::window window;
        std::unique_ptr<base::x11::event_filter> filter;
    } keyboard_intercept;
};

}
