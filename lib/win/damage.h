/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include <win/scene.h>

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
void add_repaint(Win& win, QRegion const& region)
{
    if (!win.space.base.mod.render->scene) {
        return;
    }
    win.render_data.repaints_region += region;
    acquire_repaint_outputs(win, region.translated(win.geo.pos()));
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win, typename Output>
void reset_repaints(Win& win, Output const* output)
{
    auto reset_all = [&win] {
        win.render_data.repaints_region = {};
        win.render_data.layer_repaints_region = {};
    };

    assert(output);

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
void set_ready_for_painting(Win& win)
{
    if (win.render_data.ready_for_painting) {
        return;
    }

    win.render_data.ready_for_painting = true;

    if (win.space.base.mod.render->scene) {
        add_full_repaint(win);
        Q_EMIT win.qobject->windowShown();
    }
}

}
