/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "base/wayland/output.h"

#include <wayland-server-core.h>

struct wlr_output;

namespace KWin::base::backend::wlroots
{

class platform;

class KWIN_EXPORT output : public base::wayland::output
{
public:
    output(wlr_output* wlr_out, wlroots::platform* platform);
    ~output() override;

    void update_enablement(bool enable) override;
    void update_dpms(base::dpms_mode mode) override;
    void update_mode(int mode_index) override;
    void update_transform(base::wayland::output_transform transform) override;

    int gamma_ramp_size() const override;
    bool set_gamma_ramp(gamma_ramp const& gamma) override;

    wlr_output* native;
    wlroots::platform* platform;

private:
    bool disable_native();
    base::event_receiver<output> destroy_rec;
};

}
