/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xwl_window.h"

#include "workspace.h"

#include <kwin_export.h>
#include <memory>

namespace Wrapland::Server
{
class PlasmaShellSurface;
class Surface;
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

class KWIN_EXPORT space : public Workspace
{
    Q_OBJECT
public:
    using x11_window = xwl_window;

    space(base::wayland::server* server);
    ~space() override;

    QRect get_icon_geometry(Toplevel const* win) const override;

    window* find_window(Wrapland::Server::Surface* surface) const;

    void handle_wayland_window_shown(Toplevel* window);
    void handle_window_added(wayland::window* window);
    void handle_window_removed(wayland::window* window);

    base::wayland::server* server;
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
