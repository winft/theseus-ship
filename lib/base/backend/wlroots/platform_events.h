/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/backend/wlroots/output.h>
#include <base/logging.h>
#include <base/utils.h>

namespace KWin::base::backend::wlroots
{

template<typename Platform>
static void handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    event_receiver<Platform>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto wlr = event_receiver_struct->receiver;

    wlr->backend = nullptr;
}

template<typename Platform>
void add_new_output(Platform& platform, wlr_output* native)
{
    auto& render = static_cast<typename Platform::render_t&>(*platform.render);
    wlr_output_init_render(native, render.allocator, render.renderer);

    if (!wl_list_empty(&native->modes)) {
        auto mode = wlr_output_preferred_mode(native);
        wlr_output_set_mode(native, mode);
        wlr_output_enable(native, true);
        if (!wlr_output_test(native)) {
            throw std::runtime_error("wlr_output_test failed");
        }
        if (!wlr_output_commit(native)) {
            throw std::runtime_error("wlr_output_commit failed");
        }
    }

    auto output = new wlroots::output(native, &platform);

    if (platform.align_horizontal) {
        auto shifted_geo = output->geometry();
        auto screens_width = 0;
        for (auto out : platform.outputs) {
            // +1 for QRect's bottom-right deviation
            screens_width = std::max(out->geometry().right() + 1, screens_width);
        }
        shifted_geo.moveLeft(screens_width);
        output->force_geometry(shifted_geo);
    }

    platform.all_outputs.push_back(output);
    platform.outputs.push_back(output);
    platform.server->output_manager->commit_changes();

    Q_EMIT platform.output_added(output);
}

template<typename Platform>
void handle_new_output(struct wl_listener* listener, void* data)
{
    base::event_receiver<Platform>* new_output_struct
        = wl_container_of(listener, new_output_struct, event);
    auto platform = new_output_struct->receiver;
    auto native = reinterpret_cast<wlr_output*>(data);

    if (native->non_desktop) {
        platform->non_desktop_outputs.push_back(new non_desktop_output(native, platform));
        return;
    }

    try {
        add_new_output(*platform, native);
    } catch (std::runtime_error const& e) {
        qCWarning(KWIN_CORE) << "Adding new output" << native->name << "failed:" << e.what();
    }
}

}
