/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <epoxy/glx.h>
#include <memory>

namespace KWin::render::backend::x11
{

struct glx_data {
    Display* display{nullptr};
    GLXWindow window{None};
    GLXContext context{nullptr};
    GLXFBConfig fbconfig{nullptr};

    using swap_interval_mesa_func = int (*)(unsigned int interval);
    swap_interval_mesa_func swap_interval_mesa{nullptr};

    struct {
        bool mesa_copy_sub_buffer{false};
        bool mesa_swap_control{false};
        bool ext_swap_control{false};
    } extensions;
};

}
