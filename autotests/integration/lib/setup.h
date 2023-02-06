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
    using wayland_space = win::wayland::space<base::wayland::platform>;
    using base_t = base::backend::wlroots::platform;

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
    void set_outputs(std::vector<Test::output> const& outputs);

    void add_client(Test::global_selection globals);

    std::unique_ptr<base_t> base;

    wlr_pointer* pointer{nullptr};
    wlr_keyboard* keyboard{nullptr};
    wlr_touch* touch{nullptr};

    std::vector<Test::client> clients;
    bool ready{false};

private:
    void handle_server_addons_created();
    void create_xwayland();
};

}

namespace KWin::Test
{

detail::test::setup* app();

}
