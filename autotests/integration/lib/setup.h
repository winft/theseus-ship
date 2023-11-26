/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Needs to be included first to override Qt macros
#include "catch_macros.h"

#include "client.h"
#include "helpers.h"
#include "types.h"

#include "base/backend/wlroots/platform.h"
#include "base/types.h"
#include "base/wayland/server.h"

#include <memory>
#include <vector>

struct wlr_pointer;
struct wlr_keyboard;
struct wlr_touch;

namespace KWin::detail::test
{

struct setup final {
#if USE_XWL
    using base_t = base::wayland::xwl_platform;
#else
    using base_t = base::wayland::platform;
#endif

    setup(std::string const& test_name);
    setup(std::string const& test_name, base::operation_mode mode);
    setup(std::string const& test_name,
          base::operation_mode mode,
          base::wayland::start_options flags);
    ~setup();

    void start();

    /// Sets @ref count horizontally lined up outputs with a default size of 1280x1024 at scale 1.
    void set_outputs(size_t count);
    void set_outputs(std::vector<QRect> const& geometries);
    void set_outputs(std::vector<output> const& outputs);

    void add_client(global_selection globals);

    std::unique_ptr<base_t> base;

    wlr_pointer* pointer{nullptr};
    wlr_keyboard* keyboard{nullptr};
    wlr_touch* touch{nullptr};

    std::vector<client> clients;
    bool ready{false};

private:
    void create_xwayland();
};

setup* app();

}
