/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QPoint>
#include <QRect>
#include <algorithm>
#include <climits>
#include <cstddef>
#include <vector>

namespace KWin::base
{

template<typename Output>
QPoint output_physical_dpi(Output const& output)
{
    auto x = output.geometry().width() / (double)output.physical_size().width() * 25.4;
    auto y = output.geometry().height() / (double)output.physical_size().height() * 25.4;

    // TODO(romangg): std::round instead?
    return {static_cast<int>(x), static_cast<int>(y)};
}

template<typename Output>
std::vector<Output*> get_intersecting_outputs(std::vector<Output*> const& outputs,
                                              QRect const& rect)
{
    std::vector<Output*> intersec;
    for (auto output : outputs) {
        if (output->geometry().intersects(rect)) {
            intersec.push_back(output);
        }
    }
    return intersec;
}

template<typename Output>
auto get_nearest_output(std::vector<Output*> const& outputs, QPoint const& pos) -> Output*
{
    Output* best_output{nullptr};
    auto min_distance = INT_MAX;

    for (auto const& output : outputs) {
        auto const& geo = output->geometry();
        if (geo.contains(pos)) {
            return output;
        }

        auto distance = QPoint(geo.topLeft() - pos).manhattanLength();
        distance = qMin(distance, QPoint(geo.topRight() - pos).manhattanLength());
        distance = qMin(distance, QPoint(geo.bottomRight() - pos).manhattanLength());
        distance = qMin(distance, QPoint(geo.bottomLeft() - pos).manhattanLength());

        if (distance < min_distance) {
            min_distance = distance;
            best_output = output;
        }
    }

    return best_output;
}

template<typename Base>
void update_output_topology(Base& base)
{
    auto& topo = base.topology;
    auto const old_topo = topo;

    QRect bounding;
    double max_scale{1.};

    for (auto& output : base.outputs) {
        bounding = bounding.united(output->geometry());
        max_scale = qMax(max_scale, output->scale());
    }

    if (topo.size != bounding.size()) {
        topo.size = bounding.size();
    }

    if (!qFuzzyCompare(topo.max_scale, max_scale)) {
        topo.max_scale = max_scale;
    }

    Q_EMIT base.topology_changed(old_topo, base.topology);
}

template<typename Base, typename Output>
void set_current_output(Base& base, Output* output)
{
    if (base.topology.current == output) {
        return;
    }
    auto old_topo = base.topology;
    base.topology.current = output;
    Q_EMIT base.current_output_changed(old_topo.current, output);
}

template<typename Base>
void set_current_output_by_position(Base& base, QPoint const& pos)
{
    set_current_output(base, get_nearest_output(base.outputs, pos));
}

template<typename Output>
Output* get_output(std::vector<Output*> const& outputs, int index)
{
    if (index >= static_cast<int>(outputs.size()) || index < 0) {
        return nullptr;
    }
    return outputs.at(index);
}

template<typename Output>
size_t get_output_index(std::vector<Output*> const& outputs, Output const& output)
{
    auto it = std::find(outputs.begin(), outputs.end(), &output);
    return it - outputs.begin();
}

}
