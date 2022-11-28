/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "mouse_intercept_filter.h"

#include "effect/blur_integration.h"
#include "effect/contrast_integration.h"
#include "effect/kscreen_integration.h"
#include "effect/slide_integration.h"

#include "base/x11/xcb/window.h"
#include "render/effects.h"
#include "win/x11/space.h"

#include <kwinxrender/utils.h>

#include <QDesktopWidget>
#include <memory.h>

namespace KWin::render::x11
{

template<typename Compositor>
class effects_handler_impl : public render::effects_handler_impl<Compositor>
{
public:
    using type = effects_handler_impl<Compositor>;

    effects_handler_impl(Compositor& compositor)
        : render::effects_handler_impl<Compositor>(compositor)
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
        auto const c = this->compositor.platform.base.space->input->cursor->x11_cursor(shape);
        if (c != XCB_CURSOR_NONE) {
            mouse_intercept.window.define_cursor(c);
        }
    }

    QImage blit_from_framebuffer(QRect const& geometry, double scale) const override
    {
#if defined(KWIN_HAVE_XRENDER_COMPOSITING)
        if (this->compositingType() == XRenderCompositing) {
            auto image = xrender_picture_to_image(this->xrenderBufferPicture(), geometry);
            image.setDevicePixelRatio(scale);
            return image;
        }
#endif

        // Provides OpenGL blits.
        return render::effects_handler_impl<Compositor>::blit_from_framebuffer(geometry, scale);
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
        bool ret = base::x11::grab_keyboard();
        if (!ret)
            return false;
        // Workaround for Qt 5.9 regression introduced with 2b34aefcf02f09253473b096eb4faffd3e62b5f4
        // we no longer get any events for the root window, one needs to call winId() on the desktop
        // window
        // TODO: change effects event handling to create the appropriate QKeyEvent without relying
        // on Qt as it's done already in the Wayland case.
        qApp->desktop()->winId();
        return ret;
    }

    void doUngrabKeyboard() override
    {
        base::x11::ungrab_keyboard();
    }

    void doStartMouseInterception(Qt::CursorShape shape) override
    {
        // NOTE: it is intended to not perform an XPointerGrab on X11. See documentation in
        // kwineffects.h The mouse grab is implemented by using a full screen input only window
        if (!mouse_intercept.window.is_valid()) {
            auto const& space_size = this->compositor.platform.base.topology.size;
            const QRect geo(0, 0, space_size.width(), space_size.height());
            const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
            const uint32_t values[] = {true,
                                       XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                           | XCB_EVENT_MASK_POINTER_MOTION};
            mouse_intercept.window.reset(base::x11::xcb::create_input_window(geo, mask, values));
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
        this->compositor.space->edges->ensureOnTop();
    }

    void doStopMouseInterception() override
    {
        mouse_intercept.window.unmap();
        mouse_intercept.filter.reset();
        win::x11::stack_screen_edges_under_override_redirect(
            this->compositor.platform.base.space.get());
    }

    void doCheckInputWindowStacking() override
    {
        mouse_intercept.window.raise();

        // Raise electric border windows above the input windows
        // so they can still be triggered. TODO: Do both at once.
        this->compositor.platform.base.space->edges->ensureOnTop();
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
};

}