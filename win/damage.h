/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "utils/algorithm.h"

#include <QRegion>

namespace KWin::win
{

template<typename Win>
QRegion repaints(Win const& win)
{
    return win.render_data.repaints_region.translated(win.geo.pos())
        | win.render_data.layer_repaints_region;
}

template<typename Win>
void acquire_repaint_outputs(Win& win, QRegion const& region)
{
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        // On X11 we do not paint per output.
        return;
    }
    for (auto& out : win.space.base.outputs) {
        if (contains(win.render_data.repaint_outputs, out)) {
            continue;
        }
        if (region.intersected(out->geometry()).isEmpty()) {
            continue;
        }
        win.render_data.repaint_outputs.push_back(out);
    }
}

template<typename Win>
void add_repaint(Win& win, QRegion const& region)
{
    if (!win.space.base.render->compositor->scene) {
        return;
    }
    win.render_data.repaints_region += region;
    acquire_repaint_outputs(win, region.translated(win.geo.pos()));
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win>
void add_layer_repaint(Win& win, QRegion const& region)
{
    if (!win.space.base.render->compositor->scene) {
        return;
    }
    win.render_data.layer_repaints_region += region;
    acquire_repaint_outputs(win, region);
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win>
void add_full_repaint(Win& win)
{
    auto const region = visible_rect(&win);
    win.render_data.repaints_region = region.translated(-win.geo.pos());

    for (auto child : win.transient->children) {
        if (child->transient->annexed) {
            add_full_repaint(*child);
        }
    }

    acquire_repaint_outputs(win, region);
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win, typename Output>
void reset_repaints(Win& win, Output const* output)
{
    auto reset_all = [&win] {
        win.render_data.repaints_region = {};
        win.render_data.layer_repaints_region = {};
    };

    if (!output) {
        assert(!win.render_data.repaint_outputs.size());
        reset_all();
        return;
    }

    remove_all(win.render_data.repaint_outputs, output);

    if (win.render_data.repaint_outputs.empty()) {
        reset_all();
        return;
    }

    auto reset_region = QRegion(output->geometry());

    for (auto out : win.render_data.repaint_outputs) {
        reset_region = reset_region.subtracted(out->geometry());
    }

    win.render_data.repaints_region.translate(win.geo.pos());
    win.render_data.repaints_region = win.render_data.repaints_region.subtracted(reset_region);
    win.render_data.repaints_region.translate(-win.geo.pos());

    win.render_data.layer_repaints_region
        = win.render_data.layer_repaints_region.subtracted(reset_region);
}

template<typename Win>
void add_full_damage(Win& win)
{
    if (!win.space.base.render->compositor->scene) {
        return;
    }

    auto const render_geo = frame_to_render_rect(&win, win.geo.frame);

    auto const damage = QRect({}, render_geo.size());
    win.render_data.damage_region = damage;

    auto repaint = damage;
    if (win.geo.has_in_content_deco) {
        repaint.translate(-QPoint(left_border(&win), top_border(&win)));
    }
    win.render_data.repaints_region |= repaint;
    acquire_repaint_outputs(win, render_geo);

    Q_EMIT win.qobject->damaged(win.render_data.damage_region);
}

template<typename Win>
void discard_buffer(Win& win)
{
    add_full_damage(win);
    if (win.render) {
        win.render->discard_buffer();
    }
}

template<typename Win>
void set_ready_for_painting(Win& win)
{
    if (win.render_data.ready_for_painting) {
        return;
    }

    win.render_data.ready_for_painting = true;

    if (win.space.base.render->compositor->scene) {
        add_full_repaint(win);
        Q_EMIT win.qobject->windowShown();
    }
}

}
