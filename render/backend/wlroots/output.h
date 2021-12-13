/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/backend/wlroots/output.h"
#include "base/utils.h"

#include <Wrapland/Server/drm_lease_v1.h>

#include <wayland-server-core.h>

struct wlr_output;

namespace KWin::render::backend::wlroots
{

class egl_output;

class output
{

public:
    output(base::backend::wlroots::output const& base);
    ~output();

    void reset();
    void disable();

    std::unique_ptr<egl_output> egl;
    base::backend::wlroots::output const& base;

private:
    base::event_receiver<output> present_rec;
};

}
