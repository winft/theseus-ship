/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/backend/wlroots/output.h"
#include "base/utils.h"
#include "render/wayland/output.h"

#include <Wrapland/Server/drm_lease_v1.h>

struct wlr_output;

namespace KWin::render::backend::wlroots
{

class egl_output;
class qpainter_output;

class output : public wayland::output
{
public:
    output(base::backend::wlroots::output& base, render::platform& platform);
    ~output();

    void reset();
    void disable();

    std::unique_ptr<egl_output> egl;
    std::unique_ptr<qpainter_output> qpainter;

private:
    base::event_receiver<output> present_rec;
    base::event_receiver<output> frame_rec;
};

}
