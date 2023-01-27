/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config.h"
#include "options.h"
#include "output.h"
#include "output_topology.h"
#include "seat/session.h"
#include "types.h"
#include "x11/data.h"

#include "kwin_export.h"

#include <QObject>
#include <memory>
#include <vector>

namespace KWin::base
{

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    platform(base::config config);
    ~platform() override;

    virtual clockid_t get_clockid() const;

    /// Makes a copy of all outputs. Only for external use. Prefer subclass objects instead.
    virtual std::vector<output*> get_outputs() const = 0;

    base::operation_mode operation_mode;
    output_topology topology;
    base::config config;
    base::x11::data x11_data;

    std::unique_ptr<base::options> options;
    std::unique_ptr<base::seat::session> session;

private:
    Q_DISABLE_COPY(platform)

Q_SIGNALS:
    void output_added(KWin::base::output*);
    void output_removed(KWin::base::output*);
    void topology_changed(output_topology const& old_topo, output_topology const& topo);

    // TODO(romangg): Either remove since it's only used in a test or find a better way to design
    //                the API. The current output is part of the output topology, but it shouldn't
    //                reuse the topology_changed signal, as this implies too much of a change.
    void current_output_changed(KWin::base::output const* old, KWin::base::output const* current);
};

}
