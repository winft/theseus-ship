/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "helpers.h"

#include "base/backend/wlroots.h"
#include "base/platform.h"
#include "main.h"
#include "render/backend/wlroots/backend.h"
#include "wayland_server.h"

#include <memory>
#include <vector>

struct wlr_input_device;

namespace KWin
{
namespace render::wayland
{
class compositor;
}
namespace win::wayland
{
class space;
}
namespace xwl
{
class xwayland;
}

class KWIN_EXPORT WaylandTestApplication : public ApplicationWaylandAbstract
{
    Q_OBJECT
public:
    wayland_base base;
    std::unique_ptr<WaylandServer> server;
    std::unique_ptr<xwl::xwayland> xwayland;
    std::unique_ptr<win::wayland::space> workspace;

    wlr_input_device* pointer{nullptr};
    wlr_input_device* keyboard{nullptr};
    wlr_input_device* touch{nullptr};

    std::vector<Test::client> clients;

    WaylandTestApplication(OperationMode mode,
                           std::string const& socket_name,
                           wayland_start_options flags,
                           int& argc,
                           char** argv);
    ~WaylandTestApplication() override;

    bool is_screen_locked() const override;

    wayland_base& get_base() override;
    WaylandServer* get_wayland_server() override;
    render::compositor* get_compositor() override;
    debug::console* create_debug_console() override;

    void start();

private:
    void handle_server_addons_created();
    void create_xwayland();

    std::unique_ptr<render::backend::wlroots::backend> render;
    std::unique_ptr<render::wayland::compositor> compositor;
};

}
