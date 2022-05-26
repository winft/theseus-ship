/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/server.h"
#include "xwl_window.h"

#include "kwin_export.h"
#include "win/space.h"

#include <memory>

namespace Wrapland::Server
{
class AppmenuManager;
class Compositor;
class IdleInhibitManagerV1;
class KdeIdle;
class LayerShellV1;
class PlasmaShell;
class PlasmaShellSurface;
class PlasmaVirtualDesktopManager;
class PlasmaWindowManager;
class ServerSideDecorationPaletteManager;
class Subcompositor;
class Surface;
class XdgActivationV1;
class XdgDecorationManager;
class XdgForeign;
class XdgShell;
}

namespace KWin
{

namespace win
{

namespace x11
{
class window;
}

class virtual_desktop;

namespace wayland
{
class window;
struct xdg_activation;

class KWIN_EXPORT space : public win::space
{
    Q_OBJECT
public:
    using x11_window = xwl_window;

    space(render::compositor& render, base::wayland::server* server);
    ~space() override;

    Toplevel* findInternal(QWindow* window) const override;
    QRect get_icon_geometry(Toplevel const* win) const override;

    window* find_window(Wrapland::Server::Surface* surface) const;

    void handle_wayland_window_shown(Toplevel* window);
    void handle_window_added(wayland::window* window);
    void handle_window_removed(wayland::window* window);

    base::wayland::server* server;

    std::unique_ptr<Wrapland::Server::Compositor> compositor;
    std::unique_ptr<Wrapland::Server::Subcompositor> subcompositor;
    std::unique_ptr<Wrapland::Server::XdgShell> xdg_shell;
    std::unique_ptr<Wrapland::Server::LayerShellV1> layer_shell;

    std::unique_ptr<Wrapland::Server::XdgDecorationManager> xdg_decoration_manager;
    std::unique_ptr<Wrapland::Server::XdgActivationV1> xdg_activation;
    std::unique_ptr<Wrapland::Server::XdgForeign> xdg_foreign;

    std::unique_ptr<Wrapland::Server::PlasmaShell> plasma_shell;
    std::unique_ptr<Wrapland::Server::PlasmaWindowManager> plasma_window_manager;
    std::unique_ptr<Wrapland::Server::PlasmaVirtualDesktopManager> plasma_virtual_desktop_manager;

    std::unique_ptr<Wrapland::Server::KdeIdle> kde_idle;
    std::unique_ptr<Wrapland::Server::IdleInhibitManagerV1> idle_inhibit_manager_v1;

    std::unique_ptr<Wrapland::Server::AppmenuManager> appmenu_manager;
    std::unique_ptr<Wrapland::Server::ServerSideDecorationPaletteManager>
        server_side_decoration_palette_manager;

    std::unique_ptr<win::wayland::xdg_activation> activation;

    QVector<Wrapland::Server::PlasmaShellSurface*> plasma_shell_surfaces;

protected:
    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        win::space_areas& areas) override;

private:
    void handle_x11_window_added(x11::window* window);
    void handle_desktop_removed(virtual_desktop* desktop);
};

}
}
}
