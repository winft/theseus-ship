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
    return win.repaints_region.translated(win.pos()) | win.layer_repaints_region;
}

template<typename Win>
void acquire_repaint_outputs(Win& win, QRegion const& region)
{
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        // On X11 we do not paint per output.
        return;
    }
    for (auto& out : win.space.base.outputs) {
        if (contains(win.repaint_outputs, out)) {
            continue;
        }
        if (region.intersected(out->geometry()).isEmpty()) {
            continue;
        }
        win.repaint_outputs.push_back(out);
    }
}

template<typename Win>
void add_repaint(Win& win, QRegion const& region)
{
    if (!win.space.base.render->compositor->scene) {
        return;
    }
    win.repaints_region += region;
    acquire_repaint_outputs(win, region.translated(win.pos()));
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win>
void add_layer_repaint(Win& win, QRegion const& region)
{
    if (!win.space.base.render->compositor->scene) {
        return;
    }
    win.layer_repaints_region += region;
    acquire_repaint_outputs(win, region);
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win>
void add_full_repaint(Win& win)
{
    auto const region = visible_rect(&win);
    win.repaints_region = region.translated(-win.pos());

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
        win.repaints_region = {};
        win.layer_repaints_region = {};
    };

    if (!output) {
        assert(!win.repaint_outputs.size());
        reset_all();
        return;
    }

    remove_all(win.repaint_outputs, output);

    if (win.repaint_outputs.empty()) {
        reset_all();
        return;
    }

    auto reset_region = QRegion(output->geometry());

    for (auto out : win.repaint_outputs) {
        reset_region = reset_region.subtracted(out->geometry());
    }

    win.repaints_region.translate(win.pos());
    win.repaints_region = win.repaints_region.subtracted(reset_region);
    win.repaints_region.translate(-win.pos());

    win.layer_repaints_region = win.layer_repaints_region.subtracted(reset_region);
}

template<typename Win>
void add_full_damage(Win& win)
{
    if (!win.space.base.render->compositor->scene) {
        return;
    }

    auto const render_geo = frame_to_render_rect(&win, win.frameGeometry());

    auto const damage = QRect({}, render_geo.size());
    win.damage_region = damage;

    auto repaint = damage;
    if (win.has_in_content_deco) {
        repaint.translate(-QPoint(left_border(&win), top_border(&win)));
    }
    win.repaints_region |= repaint;
    acquire_repaint_outputs(win, render_geo);

    Q_EMIT win.qobject->damaged(win.damage_region);
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
    if (win.ready_for_painting) {
        return;
    }

    win.ready_for_painting = true;

    if (win.space.base.render->compositor->scene) {
        add_full_repaint(win);
        Q_EMIT win.qobject->windowShown();
    }
}

}
