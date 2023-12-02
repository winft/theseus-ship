/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/output.h>
#include <base/output_topology.h>

#include <QObject>
#include <functional>

namespace KWin::base
{

class KWIN_EXPORT platform_qobject : public QObject
{
    Q_OBJECT

public:
    platform_qobject(std::function<double()> get_scale)
        : get_scale{get_scale}
    {
    }

    std::function<double()> get_scale;

Q_SIGNALS:
    void output_added(KWin::base::output*);
    void output_removed(KWin::base::output*);
    void topology_changed(KWin::base::output_topology const& old_topo,
                          KWin::base::output_topology const& topo);

    // TODO(romangg): Either remove since it's only used in a test or find a better way to design
    //                the API. The current output is part of the output topology, but it shouldn't
    //                reuse the topology_changed signal, as this implies too much of a change.
    void current_output_changed(KWin::base::output const* old, KWin::base::output const* current);

    // Only relevant on Wayland with Xwayland being (re-)started later.
    // TODO(romangg): Move to Wayland platform?
    void x11_reset();
};

}
