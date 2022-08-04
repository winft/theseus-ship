/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effects.h"
#include "types.h"
#include "x11/compositor_selection_owner.h"

#include "base/options.h"
#include "main.h"
#include "win/remnant.h"
#include "win/space_window_release.h"

#include <xcb/composite.h>

namespace KWin::render
{

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
        sprintf(selection_name, "_NET_WM_CM_S%d", kwinApp()->x11ScreenNumber());
        comp.m_selectionOwner = new CompositorSelectionOwner(selection_name);
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
    auto con = kwinApp()->x11Connection();
    if (!con) {
        delete comp.m_selectionOwner;
        comp.m_selectionOwner = nullptr;
        return;
    }
    compositor_claim_selection(comp);
    xcb_composite_redirect_subwindows(
        con, kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
}

template<typename Compositor>
void compositor_start_scene(Compositor& comp)
{
    assert(comp.space);
    assert(!comp.scene);

    if (kwinApp()->isTerminating()) {
        // Don't start while KWin is terminating. An event to restart might be lingering
        // in the event queue due to graphics reset.
        return;
    }

    if (comp.m_state != state::off) {
        return;
    }

    comp.m_state = state::starting;
    kwinApp()->options->reloadCompositingSettings(true);
    compositor_setup_x11_support(comp);

    Q_EMIT comp.qobject->aboutToToggleCompositing();

    comp.scene = comp.create_scene();
    comp.space->stacking_order->render_restack_required = true;

    for (auto& client : comp.space->windows) {
        client->setupCompositing();
    }

    // Sets also the 'effects' pointer.
    comp.effects = comp.platform.createEffectsHandler(&comp, comp.scene.get());
    QObject::connect(comp.effects.get(),
                     &EffectsHandler::screenGeometryChanged,
                     comp.qobject.get(),
                     [&comp] { comp.addRepaintFull(); });
    QObject::connect(comp.space->stacking_order.get(),
                     &win::stacking_order::unlocked,
                     comp.qobject.get(),
                     [&comp]() {
                         if (comp.effects) {
                             comp.effects->checkInputWindowStacking();
                         }
                     });

    comp.m_state = state::on;
    Q_EMIT comp.qobject->compositingToggled(true);

    // Render at least once.
    comp.addRepaintFull();
    comp.performCompositing();
}

template<typename Compositor>
void compositor_stop(Compositor& comp, bool on_shutdown)
{
    if (comp.m_state == state::off || comp.m_state == state::stopping) {
        return;
    }
    comp.m_state = state::stopping;
    Q_EMIT comp.qobject->aboutToToggleCompositing();

    // Some effects might need access to effect windows when they are about to
    // be destroyed, for example to unreference deleted windows, so we have to
    // make sure that effect windows outlive effects.
    comp.effects.reset();

    if (comp.space) {
        for (auto& c : comp.space->windows) {
            if (c->remnant) {
                continue;
            }
            c->finishCompositing();
        }

        if (auto con = kwinApp()->x11Connection()) {
            xcb_composite_unredirect_subwindows(
                con, kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
        }
        while (!win::get_remnants(*comp.space).empty()) {
            auto win = win::get_remnants(*comp.space).front();
            win->remnant->refcount = 0;
            win::delete_window_from_space(*comp.space, win);
        }
    }

    assert(comp.scene);
    comp.scene.reset();
    comp.platform.render_stop(on_shutdown);

    comp.m_bufferSwapPending = false;
    comp.compositeTimer.stop();
    comp.repaints_region = QRegion();

    comp.m_state = state::off;
    Q_EMIT comp.qobject->compositingToggled(false);
}

}
