/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "abstract_wayland_output.h"
#include "base/utils.h"

#include <Wrapland/Server/drm_lease_v1.h>

#include <wayland-server-core.h>

struct wlr_output;

namespace KWin::render::backend::wlroots
{

class backend;
class buffer;
class output;

class KWIN_EXPORT output : public AbstractWaylandOutput
{
    Q_OBJECT
private:
    void create_lease_connector();

    std::unique_ptr<Wrapland::Server::drm_lease_connector_v1> lease_connector;

    base::event_receiver<output> destroy_rec;
    base::event_receiver<output> present_rec;

    bool disable_native();

public:
    wlr_output* native;
    backend* back;

    void updateEnablement(bool enable) override;
    void updateDpms(DpmsMode mode) override;
    void updateMode(int modeIndex) override;
    void updateTransform(Transform transform) override;

    int gammaRampSize() const override;
    bool setGammaRamp(GammaRamp const& gamma) override;

    output(wlr_output* wlr_out, backend* backend);
    ~output() override;
};

}
