/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xwl_control.h"

#include "render/wayland/buffer.h"
#include "scene.h"
#include "surface.h"
#include "win/x11/scene.h"
#include "win/x11/window.h"

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Space>
class xwl_window : public x11::window<Space>
{
public:
    using control_t = xwl_control<xwl_window>;

    xwl_window(win::remnant remnant, Space& space)
        : x11::window<Space>(std::move(remnant), space)
    {
    }

    xwl_window(xcb_window_t xcb_win, Space& space)
        : x11::window<Space>(xcb_win, space)
    {
    }

    qreal bufferScale() const override
    {
        return this->surface ? this->surface->state().scale : 1;
    }

    void handle_surface_damage(QRegion const& damage)
    {
        if (!this->ready_for_painting) {
            // avoid "setReadyForPainting()" function calling overhead
            if (this->sync_request.counter == XCB_NONE) {
                // cannot detect complete redraw, consider done now
                this->first_geo_synced = true;
                set_ready_for_painting(*this);
            }
        }
        wayland::handle_surface_damage(*this, damage);
    }

    void add_scene_window_addon() override
    {
        auto setup_buffer = [](auto& buffer) {
            using scene_t = typename Space::base_t::render_t::compositor_t::scene_t;
            using buffer_integration_t
                = render::wayland::buffer_win_integration<typename scene_t::buffer_t>;

            auto win_integrate = std::make_unique<buffer_integration_t>(buffer);
            auto update_helper = [&buffer]() {
                auto& win_integrate = static_cast<buffer_integration_t&>(*buffer.win_integration);
                update_buffer(*buffer.window->ref_win, win_integrate.external);
            };
            win_integrate->update = update_helper;
            buffer.win_integration = std::move(win_integrate);
        };
        auto get_viewport = [](auto window, auto /*contentsRect*/) {
            // XWayland client's geometry must be taken from their content placement since the
            // buffer size is not in sync. So we only consider an explicitly set source rectangle.
            return window->surface ? get_scaled_source_rectangle(*window) : QRectF();
        };

        this->render->win_integration.setup_buffer = setup_buffer;
        this->render->win_integration.get_viewport = get_viewport;

        if (this->surface) {
            setup_scale_scene_notify(*this);
        }
    }

    void setupCompositing() override
    {
        assert(!this->remnant);
        assert(this->space.base.render->compositor->scene);
        assert(this->damage_handle == XCB_NONE);

        discard_shape(*this);
        this->damage_region = QRect({}, this->geo.size());

        add_scene_window(*this->space.base.render->compositor->scene, *this);

        if (this->control) {
            // for internalKeep()
            update_visibility(this);
        }
    }
};

}
