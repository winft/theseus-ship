/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "options.h"
#include "support_properties.h"
#include "types.h"
#include "x11/compositor_selection_owner.h"

#include "win/remnant.h"
#include "win/space_window_release.h"
#include "win/stacking_order.h"

#include <render/effect/interface/effects_handler.h>

#include <xcb/composite.h>

namespace KWin::render
{

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

template<typename Compositor>
void compositor_destroy_selection(Compositor& comp)
{
    delete comp.m_selectionOwner;
    comp.m_selectionOwner = nullptr;
}

template<typename Compositor>
void compositor_claim_selection(Compositor& comp)
{
    using CompositorSelectionOwner = x11::compositor_selection_owner;

    if (!comp.m_selectionOwner) {
        char selection_name[100];
        sprintf(selection_name, "_NET_WM_CM_S%d", comp.platform.base.x11_data.screen_number);
        comp.m_selectionOwner
            = new CompositorSelectionOwner(selection_name,
                                           comp.platform.base.x11_data.connection,
                                           comp.platform.base.x11_data.root_window);
        QObject::connect(comp.m_selectionOwner,
                         &CompositorSelectionOwner::lostOwnership,
                         comp.qobject.get(),
                         [comp = &comp] { compositor_stop(*comp, false); });
    }

    if (!comp.m_selectionOwner) {
        // No X11 yet.
        return;
    }

    comp.m_selectionOwner->own();
}

template<typename Compositor>
void compositor_setup_x11_support(Compositor& comp)
{
    auto con = comp.platform.base.x11_data.connection;
    if (!con) {
        delete comp.m_selectionOwner;
        comp.m_selectionOwner = nullptr;
        return;
    }
    compositor_claim_selection(comp);
    xcb_composite_redirect_subwindows(
        con, comp.platform.base.x11_data.root_window, XCB_COMPOSITE_REDIRECT_MANUAL);
}

template<typename Compositor>
void compositor_start_scene(Compositor& comp)
{
    assert(comp.space);
    assert(!comp.scene);

    if (comp.state != state::off) {
        return;
    }

    comp.state = state::starting;
    comp.platform.options->reloadCompositingSettings(true);
    compositor_setup_x11_support(comp);

    Q_EMIT comp.qobject->aboutToToggleCompositing();

    comp.scene = comp.create_scene();
    comp.space->stacking.order.render_restack_required = true;

    for (auto& win : comp.space->windows) {
        std::visit(overload{[](auto&& win) { win->setupCompositing(); }}, win);
    }

    // Sets also the 'effects' pointer.
    comp.effects = std::make_unique<typename Compositor::effects_t>(*comp.scene);
    QObject::connect(comp.effects.get(),
                     &EffectsHandler::screenGeometryChanged,
                     comp.qobject.get(),
                     [&comp] { full_repaint(comp); });
    QObject::connect(comp.space->stacking.order.qobject.get(),
                     &win::stacking_order_qobject::unlocked,
                     comp.effects.get(),
                     [effects = comp.effects.get()]() { effects->checkInputWindowStacking(); });

    comp.state = state::on;
    Q_EMIT comp.qobject->compositingToggled(true);

    // Render at least once.
    full_repaint(comp);
    comp.performCompositing();
}

template<typename Compositor>
void compositor_stop(Compositor& comp, bool on_shutdown)
{
    if (comp.state == state::off || comp.state == state::stopping) {
        return;
    }
    comp.state = state::stopping;
    Q_EMIT comp.qobject->aboutToToggleCompositing();

    // Some effects might need access to effect windows when they are about to
    // be destroyed, for example to unreference deleted windows, so we have to
    // make sure that effect windows outlive effects.
    comp.effects.reset();

    if (comp.space) {
        for (auto& var_win : comp.space->windows) {
            std::visit(
                overload{[](auto&& win) {
                    if (win->remnant) {
                        return;
                    }
                    if constexpr (requires(decltype(win) win) { win->finishCompositing(); }) {
                        win->finishCompositing();
                    } else {
                        finish_compositing(*win);
                    }
                }},
                var_win);
        }

        if (auto con = comp.platform.base.x11_data.connection) {
            xcb_composite_unredirect_subwindows(
                con, comp.platform.base.x11_data.root_window, XCB_COMPOSITE_REDIRECT_MANUAL);
        }
        while (!win::get_remnants(*comp.space).empty()) {
            auto win = win::get_remnants(*comp.space).front();
            std::visit(overload{[&comp](auto&& win) {
                           win->remnant->refcount = 0;
                           win::delete_window_from_space(*comp.space, *win);
                       }},
                       win);
        }
    }

    assert(comp.scene);
    comp.scene.reset();
    comp.platform.render_stop(on_shutdown);

    if constexpr (requires(Compositor& comp) { comp.compositeTimer; }) {
        comp.m_bufferSwapPending = false;
        comp.compositeTimer.stop();
        comp.repaints_region = {};
    }

    comp.state = state::off;
    Q_EMIT comp.qobject->compositingToggled(false);
}

/**
 * Re-initializes the Compositor completely.
 * Connected to the D-Bus signal org.kde.KWin /KWin reinitCompositing
 */
template<typename Compositor>
void reinitialize_compositor(Compositor& comp)
{
    // Reparse config. Config options will be reloaded by start()
    comp.platform.base.config.main->reparseConfiguration();

    // Restart compositing
    compositor_stop(comp, false);

    assert(comp.space);
    comp.start(*comp.space);

    if (comp.effects) {
        // start() may fail
        comp.effects->reconfigure();
    }
}

template<typename Compositor>
void compositor_setup(Compositor& comp)
{
    QObject::connect(comp.platform.options->qobject.get(),
                     &render::options_qobject::configChanged,
                     comp.qobject.get(),
                     [&] { comp.configChanged(); });
    QObject::connect(comp.platform.options->qobject.get(),
                     &render::options_qobject::animationSpeedChanged,
                     comp.qobject.get(),
                     [&] { comp.configChanged(); });

    comp.unused_support_property_timer.setInterval(compositor_lost_message_delay);
    comp.unused_support_property_timer.setSingleShot(true);
    QObject::connect(&comp.unused_support_property_timer,
                     &QTimer::timeout,
                     comp.qobject.get(),
                     [&] { delete_unused_support_properties(comp); });
}

}
