/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"
#include "output_topology.h"

#include "kwin_export.h"
#include "screens.h"

#include <QObject>
#include <memory>
#include <vector>

namespace KWin
{

namespace render
{
class platform;
}

namespace base
{

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    platform();
    ~platform() override;

    virtual clockid_t get_clockid() const;

    /// Makes a copy of all outputs. Only for external use. Prefer subclass objects instead.
    virtual std::vector<output*> get_outputs() const = 0;

    Screens screens;
    output_topology topology;
    std::unique_ptr<render::platform> render;

private:
    Q_DISABLE_COPY(platform)

Q_SIGNALS:
    void output_added(KWin::base::output*);
    void output_removed(KWin::base::output*);
    void topology_changed(output_topology const& old_topo, output_topology const& topo);
};

}
}
