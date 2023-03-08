/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "popup_placement.h"
#include "window_release.h"

#include "base/wayland/server.h"
#include "utils/geo.h"
#include "wayland_logging.h"
#include "win/controlling.h"
#include "win/input.h"
#include "win/meta.h"
#include "win/rules/book_edit.h"
#include "win/rules/find.h"
#include "win/setup.h"
#include "win/space_areas_helpers.h"
#include "win/transient.h"

#include <KScreenLocker/KsldApp>
#include <Wrapland/Server/appmenu.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/globals.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/touch_pool.h>
#include <Wrapland/Server/xdg_decoration.h>
#include <Wrapland/Server/xdg_shell_popup.h>
#include <Wrapland/Server/xdg_shell_toplevel.h>

#include <functional>
#include <memory>

namespace KWin::win::wayland
{

template<typename Win, typename Space>
Win& create_shell_window(Space& space, Wrapland::Server::XdgShellSurface* shell_surface)
{
    namespace WS = Wrapland::Server;

    auto const surface = shell_surface->surface();

    auto win = new Win(surface, space);
    block_geometry_updates(win, true);

    QObject::connect(surface->client(), &WS::Client::disconnected, win->qobject.get(), [win] {
        destroy_window(win);
    });

    win->shell_surface = shell_surface;

    QObject::connect(space.xdg_shell.get(),
                     &WS::XdgShell::pingDelayed,
                     win->qobject.get(),
                     [win](auto serial) { handle_ping_delayed(*win, serial); });
    QObject::connect(space.xdg_shell.get(),
                     &WS::XdgShell::pingTimeout,
                     win->qobject.get(),
                     [win](auto serial) { handle_ping_timeout(*win, serial); });
    QObject::connect(space.xdg_shell.get(),
                     &WS::XdgShell::pongReceived,
                     win->qobject.get(),
                     [win](auto serial) { handle_pong(*win, serial); });

    return *win;
}

template<typename Win>
void handle_parent_changed(Win& win);
template<typename Win>
void handle_minimize_request(Win& win);
template<typename Win>
void handle_maximize_request(Win& win, bool maximized);
template<typename Win>
Wrapland::Server::XdgShellSurface::States xdg_surface_states(Win const& win);

template<typename Win>
void xdg_shell_setup_control(Win& win)
{
    if (!win.control) {
        return;
    }

    win.must_place = !win.isInitialPositionSet();

    if (win.supportsWindowRules()) {
        auto const& ctrl = win.control;

        rules::setup_rules(&win);

        auto const original_geo = win.geo.frame;
        auto const ruled_geo = ctrl->rules.checkGeometry(original_geo, true);

        if (original_geo != ruled_geo) {
            win.setFrameGeometry(ruled_geo);
        }

        maximize(&win, ctrl->rules.checkMaximize(win.geo.update.max_mode, true));

        set_desktops(
            &win,
            ctrl->rules.checkDesktops(*win.space.virtual_desktop_manager, win.topo.desktops, true));
        set_desktop_file_name(&win,
                              ctrl->rules.checkDesktopFile(ctrl->desktop_file_name, true).toUtf8());
        if (ctrl->rules.checkMinimize(ctrl->minimized, true)) {
            // No animation.
            set_minimized(&win, true, true);
        }
        set_skip_taskbar(&win, ctrl->rules.checkSkipTaskbar(ctrl->skip_taskbar(), true));
        set_skip_pager(&win, ctrl->rules.checkSkipPager(ctrl->skip_pager(), true));
        set_skip_switcher(&win, ctrl->rules.checkSkipSwitcher(ctrl->skip_switcher(), true));
        set_keep_above(&win, ctrl->rules.checkKeepAbove(ctrl->keep_above, true));
        set_keep_below(&win, ctrl->rules.checkKeepBelow(ctrl->keep_below, true));
        set_shortcut(&win, ctrl->rules.checkShortcut(ctrl->shortcut.toString(), true));
        win.updateColorScheme();

        // Don't place the client if its position is set by a rule.
        if (ctrl->rules.checkPosition(geo::invalid_point, true) != geo::invalid_point) {
            win.must_place = false;
        }

        // Remove Apply Now rules.
        rules::discard_used_rules(*win.space.rule_book, win, false);

        win.updateWindowRules(rules::type::all);
    }

    if (win.geo.update.max_mode != maximize_mode::restore || win.geo.update.fullscreen) {
        win.must_place = false;
    }
}

template<typename Win>
void xdg_shell_handle_first_commit(Win& win)
{
    namespace WS = Wrapland::Server;

    QObject::disconnect(win.surface, &WS::Surface::committed, win.qobject.get(), nullptr);
    QObject::connect(
        win.surface, &WS::Surface::committed, win.qobject.get(), [&win] { win.handle_commit(); });

    update_shadow(&win);
    QObject::connect(win.surface, &Wrapland::Server::Surface::committed, win.qobject.get(), [&win] {
        if (win.surface->state().updates & Wrapland::Server::surface_change::shadow) {
            update_shadow(&win);
        }
    });

    xdg_shell_setup_parent(win);
    xdg_shell_setup_control(win);

    block_geometry_updates(&win, false);

    if (win.pending_configures.empty()) {
        // xdg-shell protocol stipulates a single configure event on first commit.
        if (win.toplevel) {
            // TODO(romangg): Check rules for caps. But then must also be changable later.
            using cap = WS::xdg_shell_wm_capability;
            win.toplevel->set_capabilities(
                {cap::window_menu, cap::maximize, cap::fullscreen, cap::minimize});
        }

        QSize config_size(0, 0);
        if (win.must_place && get_placement_policy(win) == placement::maximizing) {
            config_size
                = space_window_area(
                      win.space, PlacementArea, get_current_output(win.space), get_desktop(win))
                      .size();
        }
        win.configure_geometry(QRect(win.geo.pos(), config_size));
    }

    win.initialized = true;
}

template<typename Win>
void update_icon(Win& win)
{
    QString const wayland_icon = QStringLiteral("wayland");
    auto const df_icon = icon_from_desktop_file(win);
    auto const icon = df_icon.isEmpty() ? wayland_icon : df_icon;
    if (icon == win.control->icon.name()) {
        return;
    }
    win.control->set_icon(QIcon::fromTheme(icon));
}

template<typename Win, typename Space>
Win& create_toplevel_window(Space* space, Wrapland::Server::XdgShellToplevel* toplevel)
{
    namespace WS = Wrapland::Server;

    auto& win = create_shell_window<Win>(*space, toplevel->surface());
    win.toplevel = toplevel;

    win.control = std::make_unique<typename Win::xdg_shell_control_t>(win);
    win.control->setup_tabbox();
    win.control->setup_color_scheme();

    setup_window_control_connections(&win);

    auto update_icon = [&win] {
        QString const wayland_icon = QStringLiteral("wayland");
        auto const df_icon = icon_from_desktop_file(&win);
        auto const icon = df_icon.isEmpty() ? wayland_icon : df_icon;
        if (icon != win.control->icon.name()) {
            win.control->icon = QIcon::fromTheme(icon);
            Q_EMIT win.qobject->iconChanged();
        }
    };

    QObject::connect(
        win.qobject.get(), &Win::qobject_t::desktopFileNameChanged, win.qobject.get(), update_icon);
    update_icon();

    QObject::connect(toplevel, &WS::XdgShellToplevel::resourceDestroyed, win.qobject.get(), [&win] {
        destroy_window(&win);
    });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::configureAcknowledged,
                     win.qobject.get(),
                     [&win](auto serial) { handle_configure_ack(win, serial); });

    win.meta.caption.normal = QString::fromStdString(toplevel->title()).simplified();
    QObject::connect(toplevel, &WS::XdgShellToplevel::titleChanged, win.qobject.get(), [&win] {
        win.handle_title_changed();
    });
    QTimer::singleShot(0, win.qobject.get(), [&win] { win.updateCaption(); });

    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::moveRequested,
                     win.qobject.get(),
                     [&win](auto seat, auto serial) { handle_move_request(win, seat, serial); });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::resizeRequested,
                     win.qobject.get(),
                     [&win](auto seat, auto serial, auto edges) {
                         handle_resize_request(win, seat, serial, edges);
                     });

    // Determine resource name, inspired by ICCCM 4.1.2.5: the binary name of the invoked client.
    QFileInfo info{QString::fromStdString(toplevel->client()->executablePath())};
    QByteArray resourceName;
    if (info.exists()) {
        resourceName = info.fileName().toUtf8();
    }
    set_wm_class(win, resourceName, toplevel->appId().c_str());
    set_desktop_file_name(&win, toplevel->appId().c_str());
    QObject::connect(toplevel, &WS::XdgShellToplevel::appIdChanged, win.qobject.get(), [&win] {
        win.handle_class_changed();
    });

    QObject::connect(toplevel, &WS::XdgShellToplevel::minimizeRequested, win.qobject.get(), [&win] {
        handle_minimize_request(win);
    });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::maximizedChanged,
                     win.qobject.get(),
                     [&win](auto maximized) { handle_maximize_request(win, maximized); });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::fullscreenChanged,
                     win.qobject.get(),
                     [&win](auto fullscreen, auto output) {
                         handle_fullscreen_request(win, fullscreen, output);
                     });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::windowMenuRequested,
                     win.qobject.get(),
                     [&win](auto seat, auto serial, auto surface_pos) {
                         handle_window_menu_request(win, seat, serial, surface_pos);
                     });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::transientForChanged,
                     win.qobject.get(),
                     [space, &win] { handle_parent_changed(win); });

    auto configure = [&win, toplevel] {
        if (win.closing) {
            return;
        }
        if (win.geo.update.block) {
            return;
        }
        auto size = win.synced_geometry.window.size();
        toplevel->configure(xdg_surface_states(win), size);
    };
    QObject::connect(
        win.qobject.get(), &window_qobject::activeChanged, win.qobject.get(), configure);
    QObject::connect(win.qobject.get(),
                     &window_qobject::clientStartUserMovedResized,
                     win.qobject.get(),
                     configure);
    QObject::connect(win.qobject.get(),
                     &window_qobject::clientFinishUserMovedResized,
                     win.qobject.get(),
                     configure);

    set_desktop(&win, win.space.virtual_desktop_manager->current());
    set_color_scheme(&win, QString());

    QObject::connect(win.surface, &Wrapland::Server::Surface::committed, win.qobject.get(), [&win] {
        xdg_shell_handle_first_commit(win);
    });

    return win;
}

template<typename Win, typename Space>
Win& create_popup_window(Space* space, Wrapland::Server::XdgShellPopup* popup)
{
    namespace WS = Wrapland::Server;

    auto& win = create_shell_window<Win>(*space, popup->surface());
    win.popup = popup;
    win.transient->annexed = true;

    QObject::connect(win.qobject.get(),
                     &Win::qobject_t::needsRepaint,
                     win.space.base.render->compositor->qobject.get(),
                     [&win] { win.space.base.render->compositor->schedule_repaint(&win); });
    QObject::connect(win.qobject.get(),
                     &Win::qobject_t::frame_geometry_changed,
                     win.qobject.get(),
                     [&win](auto old_frame_geo) {
                         auto const old_visible_geo = visible_rect(&win, old_frame_geo);
                         auto const visible_geo = visible_rect(&win, win.geo.frame);

                         add_layer_repaint(*lead_of_annexed_transient(&win),
                                           old_visible_geo.united(visible_geo));

                         if (old_visible_geo.size() != visible_geo.size()) {
                             discard_shape(win);
                         }
                     });
    QObject::connect(popup,
                     &WS::XdgShellPopup::configureAcknowledged,
                     win.qobject.get(),
                     [&win](auto serial) { handle_configure_ack(win, serial); });
    QObject::connect(popup,
                     &WS::XdgShellPopup::grabRequested,
                     win.qobject.get(),
                     [&win](auto seat, auto serial) { handle_grab_request(win, seat, serial); });
    QObject::connect(popup, &WS::XdgShellPopup::reposition, win.qobject.get(), [&win](auto token) {
        win.popup->repositioned(token);
        win.configure_geometry(win.geo.frame);
    });
    QObject::connect(popup, &WS::XdgShellPopup::resourceDestroyed, win.qobject.get(), [&win] {
        destroy_window(&win);
    });

    QObject::connect(win.surface, &Wrapland::Server::Surface::committed, win.qobject.get(), [&win] {
        xdg_shell_handle_first_commit(win);
    });

    return win;
}

template<typename Win>
void update_screen_edge(Win& win)
{
    using PSS = Wrapland::Server::PlasmaShellSurface;

    if (!win.space.edges) {
        return;
    }

    auto const& plasma_surface = win.plasma_shell_surface;

    if (!win.mapped || !plasma_surface || plasma_surface->role() != PSS::Role::Panel) {
        win.space.edges->reserve(&win, ElectricNone);
        return;
    }

    auto const is_auto_hidden
        = plasma_surface->panelBehavior() == PSS::PanelBehavior::AutoHide && win.hidden;
    auto const can_get_covered
        = plasma_surface->panelBehavior() == PSS::PanelBehavior::WindowsCanCover;

    if (!is_auto_hidden && !can_get_covered) {
        // Simple case with space being reserved for the panel.
        win.space.edges->reserve(&win, ElectricNone);
        return;
    }

    // We need an edge for the screen edge API, so figure out which edge the window borders.
    Qt::Edges edges;
    auto const geometry = win.geo.frame;

    for (auto output : win.space.base.outputs) {
        auto const screen_geo = output->geometry();
        if (screen_geo.left() == geometry.left()) {
            edges |= Qt::LeftEdge;
        }
        if (screen_geo.right() == geometry.right()) {
            edges |= Qt::RightEdge;
        }
        if (screen_geo.top() == geometry.top()) {
            edges |= Qt::TopEdge;
        }
        if (screen_geo.bottom() == geometry.bottom()) {
            edges |= Qt::BottomEdge;
        }
    }

    // A panel might border opposite edges, for example a full-width horizontal panel at the bottom
    // also borders left and right edges. Remove opposing edges and simplify to the remaining one.
    if (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::RightEdge)) {
        edges = edges & (~(Qt::LeftEdge | Qt::RightEdge));
    }
    if (edges.testFlag(Qt::TopEdge) && edges.testFlag(Qt::BottomEdge)) {
        edges = edges & (~(Qt::TopEdge | Qt::BottomEdge));
    }

    // A panel might still border two edges, for example a left-aligned half-width bottom panel
    // also borders the left edge. In that case use the edge with more coverage
    auto reduce = [&geometry](Qt::Edges& edges, Qt::Edge horiz, Qt::Edge vert) {
        if (!edges.testFlag(horiz) || !edges.testFlag(vert)) {
            return;
        }
        if (geometry.width() >= geometry.height()) {
            edges &= ~horiz;
        } else {
            edges &= ~vert;
        }
    };
    reduce(edges, Qt::LeftEdge, Qt::TopEdge);
    reduce(edges, Qt::LeftEdge, Qt::BottomEdge);
    reduce(edges, Qt::RightEdge, Qt::TopEdge);
    reduce(edges, Qt::RightEdge, Qt::BottomEdge);

    auto border = ElectricNone;
    if (edges.testFlag(Qt::LeftEdge)) {
        border = ElectricLeft;
    }
    if (edges.testFlag(Qt::RightEdge)) {
        border = ElectricRight;
    }
    if (edges.testFlag(Qt::TopEdge)) {
        border = ElectricTop;
    }
    if (edges.testFlag(Qt::BottomEdge)) {
        border = ElectricBottom;
    }
    win.space.edges->reserve(&win, border);
}

template<typename Win>
void install_plasma_shell_surface(Win& win, Wrapland::Server::PlasmaShellSurface* surface)
{
    using PSS = Wrapland::Server::PlasmaShellSurface;

    win.plasma_shell_surface = surface;

    QObject::connect(surface, &PSS::resourceDestroyed, win.qobject.get(), [&win] {
        win.plasma_shell_surface = nullptr;
    });

    auto update_position = [&win, surface] {
        win.setFrameGeometry(QRect(surface->position(), win.geo.update.frame.size()));
    };
    auto update_role = [&win, surface] {
        auto type = win_type::unknown;
        switch (surface->role()) {
        case PSS::Role::Desktop:
            type = win_type::desktop;
            break;
        case PSS::Role::Panel:
            type = win_type::dock;
            break;
        case PSS::Role::OnScreenDisplay:
            type = win_type::on_screen_display;
            break;
        case PSS::Role::Notification:
            type = win_type::notification;
            break;
        case PSS::Role::ToolTip:
            type = win_type::tooltip;
            break;
        case PSS::Role::CriticalNotification:
            type = win_type::critical_notification;
            break;
        case PSS::Role::AppletPopup:
            type = win_type::applet_popup;
            break;
        case PSS::Role::Normal:
        default:
            type = win_type::normal;
            break;
        }
        if (type != win.window_type) {
            win.window_type = type;
            if (type == win_type::desktop || type == win_type::dock
                || type == win_type::on_screen_display || type == win_type::notification
                || type == win_type::tooltip || type == win_type::critical_notification
                || type == win_type::applet_popup) {
                set_on_all_desktops(&win, true);
            }
            win::update_space_areas(win.space);
        }
    };

    win.must_place = false;
    update_role();
    update_screen_edge(win);

    if (surface->isPositionSet()) {
        update_position();
    }

    QObject::connect(surface, &PSS::positionChanged, win.qobject.get(), update_position);
    QObject::connect(surface, &PSS::roleChanged, win.qobject.get(), update_role);
    QObject::connect(surface, &PSS::panelBehaviorChanged, win.qobject.get(), [&win] {
        update_screen_edge(win);
        win::update_space_areas(win.space);
    });
    QObject::connect(win.qobject.get(),
                     &Win::qobject_t::frame_geometry_changed,
                     win.qobject.get(),
                     [&win] { update_screen_edge(win); });

    if (win.control) {
        QObject::connect(surface, &PSS::panelAutoHideHideRequested, win.qobject.get(), [&win] {
            if (win.plasma_shell_surface->panelBehavior() == PSS::PanelBehavior::AutoHide) {
                win.hideClient(true);
                win.plasma_shell_surface->hideAutoHidingPanel();
            }
            update_screen_edge(win);
        });
        QObject::connect(surface, &PSS::panelAutoHideShowRequested, win.qobject.get(), [&win] {
            win.hideClient(false);
            win.space.edges->reserve(&win, ElectricNone);
            win.plasma_shell_surface->showAutoHidingPanel();
        });

        set_skip_taskbar(&win, surface->skipTaskbar());
        set_skip_switcher(&win, surface->skipSwitcher());

        QObject::connect(surface, &PSS::skipTaskbarChanged, win.qobject.get(), [&win] {
            set_skip_taskbar(&win, win.plasma_shell_surface->skipTaskbar());
        });
        QObject::connect(surface, &PSS::skipSwitcherChanged, win.qobject.get(), [&win] {
            set_skip_switcher(&win, win.plasma_shell_surface->skipSwitcher());
        });
        QObject::connect(surface, &PSS::open_under_cursor_requested, win.qobject.get(), [&win] {
            win.must_place = true;
        });
    }
}

template<typename Win>
void install_appmenu(Win& win, Wrapland::Server::Appmenu* menu)
{
    using Menu = Wrapland::Server::Appmenu;

    auto update = [&win](Menu::InterfaceAddress address) {
        win.control->update_application_menu(
            {address.serviceName.toStdString(), address.objectPath.toStdString()});
    };

    QObject::connect(menu,
                     &Menu::addressChanged,
                     win.qobject.get(),
                     [update](Menu::InterfaceAddress address) { update(address); });
    update(menu->address());
}

template<typename Win>
void install_palette(Win& win, Wrapland::Server::ServerSideDecorationPalette* palette)
{
    using Palette = Wrapland::Server::ServerSideDecorationPalette;

    win.palette = palette;

    auto update = [&win](auto const& palette) {
        set_color_scheme(&win, win.control->rules.checkDecoColor(palette));
    };

    QObject::connect(palette, &Palette::paletteChanged, win.qobject.get(), [update](auto name) {
        update(name);
    });
    QObject::connect(
        palette, &QObject::destroyed, win.qobject.get(), [update] { update(QString()); });

    update(palette->palette());
}

template<typename Win>
void install_deco(Win& win, Wrapland::Server::XdgDecoration* deco)
{
    using Deco = Wrapland::Server::XdgDecoration;

    assert(win.control);
    win.xdg_deco = deco;

    QObject::connect(deco, &Deco::resourceDestroyed, win.qobject.get(), [&win] {
        win.xdg_deco = nullptr;
        if (win.closing) {
            return;
        }
        win.updateDecoration(true);
    });

    QObject::connect(deco, &Deco::modeRequested, win.qobject.get(), [&win] {
        // Force as we must send a new configure response.
        win.updateDecoration(false, true);
    });
}

template<typename Window, typename Space>
void handle_new_toplevel(Space* space, Wrapland::Server::XdgShellToplevel* toplevel)
{
    if (toplevel->client() == space->base.server->screen_locker_client_connection) {
        ScreenLocker::KSldApp::self()->lockScreenShown();
    }
    auto& win = win::wayland::create_toplevel_window<Window>(space, toplevel);

    // TODO(romangg): Also relevant for popups?
    auto it = std::find_if(
        space->plasma_shell_surfaces.begin(),
        space->plasma_shell_surfaces.end(),
        [&win](auto shell_surface) { return win.surface == shell_surface->surface(); });
    if (it != space->plasma_shell_surfaces.end()) {
        install_plasma_shell_surface(win, *it);
        space->plasma_shell_surfaces.erase(it);
    }

    if (auto menu = space->appmenu_manager->appmenuForSurface(win.surface)) {
        install_appmenu(win, menu);
    }
    if (auto palette = space->server_side_decoration_palette_manager->paletteForSurface(
            toplevel->surface()->surface())) {
        install_palette(win, palette);
    }

    space->windows.push_back(&win);

    if (win.render_data.ready_for_painting) {
        space->handle_window_added(&win);
    }

    // Not directly connected as the connection is tied to client instead of this.
    // TODO(romangg): What does this mean?
    QObject::connect(space->xdg_foreign.get(),
                     &Wrapland::Server::XdgForeign::parentChanged,
                     win.qobject.get(),
                     [space, &win](auto /*parent*/, auto child) {
                         if (child == win.surface) {
                             handle_parent_changed(win);
                         }
                     });
}

template<typename Window, typename Space>
void handle_new_popup(Space* space, Wrapland::Server::XdgShellPopup* popup)
{
    auto& win = win::wayland::create_popup_window<Window>(space, popup);
    space->windows.push_back(&win);

    if (win.render_data.ready_for_painting) {
        space->handle_window_added(&win);
    }
}

template<typename Win>
Wrapland::Server::XdgShellSurface::States xdg_surface_states(Win const& win)
{
    using XSS = Wrapland::Server::XdgShellSurface;

    XSS::States states;

    if (win.control->active) {
        states |= XSS::State::Activated;
    }
    if (win.synced_geometry.fullscreen) {
        states |= XSS::State::Fullscreen;
    }
    if (win.synced_geometry.max_mode == win::maximize_mode::full) {
        states |= XSS::State::Maximized;
    }
    if (is_resize(&win)) {
        states |= XSS::State::Resizing;
    }
    if (flags(win.control->quicktiling & quicktiles::left)) {
        states |= XSS::State::TiledLeft;
    }
    if (flags(win.control->quicktiling & quicktiles::right)) {
        states |= XSS::State::TiledRight;
    }
    if (flags(win.control->quicktiling & quicktiles::top)) {
        states |= XSS::State::TiledTop;
    }
    if (flags(win.control->quicktiling & quicktiles::bottom)) {
        states |= XSS::State::TiledBottom;
    }
    return states;
}

template<typename Win>
QRect xdg_shell_get_popup_placement(Win const& win, QRect const& bounds)
{
    // Note: Currently Qt clients don't seem to set any constraint adjustments at all.

    auto transient_lead = win.transient->lead();
    assert(transient_lead);

    auto getter = get_popup_placement<std::remove_pointer_t<decltype(transient_lead)>>;
    auto const positioner = win.popup->get_positioner();
    return getter({transient_lead,
                   bounds,
                   positioner.anchor.rect,
                   positioner.anchor.edge,
                   positioner.gravity,
                   win.geo.update.frame.isValid() ? win.geo.update.frame.size() : positioner.size,
                   positioner.anchor.offset,
                   positioner.constraint_adjustments});
}

template<typename Win>
bool needs_configure(Win const& win)
{
    auto const& update = win.geo.update;

    if (update.max_mode != win.synced_geometry.max_mode) {
        return true;
    }
    if (update.fullscreen != win.synced_geometry.fullscreen) {
        return true;
    }

    auto ref_geo = update.frame - frame_margins(&win);

    return ref_geo.isEmpty() || ref_geo.size() != win.synced_geometry.window.size();
}

template<typename Win>
void xdg_shell_popup_reposition(Win& win)
{
    if (win.popup->get_positioner().is_reactive) {
        win.configure_geometry({});
    } else {
        win.popup->popupDone();
    }
}

template<typename Win>
void handle_configure_ack(Win& win, uint32_t serial)
{
    win.acked_configure = serial;
}

template<typename Win>
Win* xdg_shell_find_parent(Win& win)
{
    auto find = [&win](auto parent_surface) { return win.space.find_window(parent_surface); };

    if (win.toplevel) {
        if (auto parent = win.toplevel->transientFor()) {
            return find(parent->surface()->surface());
        }
    } else if (win.popup) {
        if (auto parent = win.popup->transientFor()) {
            return find(parent->surface());
        }
    }
    return find(win.space.xdg_foreign->parentOf(win.surface));
}

template<typename Win>
void xdg_shell_setup_parent(Win& win)
{
    if (win.transient->lead()) {
        // Parent already set by other protocol (for example layer shell).
        return;
    }

    if (auto parent = xdg_shell_find_parent(win)) {
        parent->transient->add_child(&win);
    }
}

template<typename Win>
void handle_parent_changed(Win& win)
{
    auto parent = xdg_shell_find_parent(win);

    if (auto lead = win.transient->lead(); parent != lead) {
        // Remove from main client.
        if (lead) {
            lead->transient->remove_child(&win);
        }
        if (parent) {
            parent->transient->add_child(&win);
        }
    }
}

template<typename Win>
void handle_move_request(Win& win, Wrapland::Server::Seat* seat, uint32_t serial)
{
    if (!seat->pointers().has_implicit_grab(serial) && !seat->touches().has_implicit_grab(serial)) {
        return;
    }
    if (win.isMovable()) {
        perform_mouse_command(
            win, base::options_qobject::MouseMove, win.space.input->cursor->pos());
    }
}

template<typename Win>
void handle_resize_request(Win& win, Wrapland::Server::Seat* seat, quint32 serial, Qt::Edges edges)
{
    if (!seat->pointers().has_implicit_grab(serial) && !seat->touches().has_implicit_grab(serial)) {
        return;
    }

    if (!win.isResizable()) {
        return;
    }
    if (win.control->move_resize.enabled) {
        finish_move_resize(&win, false);
    }

    auto& mov_res = win.control->move_resize;
    mov_res.button_down = true;
    mov_res.unrestricted = false;

    // The offset describes the resize cursor contact position in frame geometry local coordinates,
    // i.e. with the origin in the top-left corner of the frame geometry.
    // Note that this might have negative coordinates if we resize by grabbing the shadow area of
    // the left or top edge.
    mov_res.offset = win.space.input->cursor->pos() - win.geo.pos();

    // The inverted offset describes the difference between bottom-right corner and offset.
    mov_res.inverted_offset
        = QPoint(win.geo.size().width() - 1, win.geo.size().height() - 1) - mov_res.offset;

    auto to_position = [edges] {
        auto pos = position::center;

        if (edges.testFlag(Qt::TopEdge)) {
            pos = position::top;
        } else if (edges.testFlag(Qt::BottomEdge)) {
            pos = position::bottom;
        }

        if (edges.testFlag(Qt::LeftEdge)) {
            pos = pos | position::left;
        } else if (edges.testFlag(Qt::RightEdge)) {
            pos = pos | position::right;
        }
        return pos;
    };
    mov_res.contact = to_position();

    if (!start_move_resize(&win)) {
        mov_res.button_down = false;
    }
    update_cursor(&win);
}

template<typename Win>
void handle_minimize_request(Win& win)
{
    perform_mouse_command(
        win, base::options_qobject::MouseMinimize, win.space.input->cursor->pos());
}

template<typename Win>
void handle_maximize_request(Win& win, bool maximized)
{
    auto const old_max_mode = win.geo.update.max_mode;
    maximize(&win, maximized ? maximize_mode::full : maximize_mode::restore);

    if (win.geo.update.max_mode == old_max_mode) {
        // No change, still send a configure event with current geometry.
        auto sync_geo = win.synced_geometry.window;
        if (sync_geo.isValid()) {
            sync_geo += frame_margins(&win);
        }
        win.configure_geometry(sync_geo);
    }
}

template<typename Win>
void handle_fullscreen_request(Win& win, bool fullscreen, Wrapland::Server::Output* /*output*/)
{
    // TODO: Consider output.
    win.setFullScreen(fullscreen, false);
}

template<typename Win>
void handle_window_menu_request(Win& win,
                                Wrapland::Server::Seat* /*seat*/,
                                quint32 /*serial*/,
                                QPoint const& surfacePos)
{
    perform_mouse_command(
        win, base::options_qobject::MouseOperationsMenu, win.geo.pos() + surfacePos);
}

template<typename Win>
void handle_grab_request(Win& win, Wrapland::Server::Seat* /*seat*/, quint32 /*serial*/)
{
    // FIXME: Check the seat and serial as well whether the parent had focus.

    win.transient->input_grab = true;
}

template<typename Win>
void handle_ping_delayed(Win& win, uint32_t serial)
{
    auto it = win.pings.find(serial);
    if (it != win.pings.end()) {
        qCDebug(KWIN_CORE) << "First ping timeout:" << caption(&win);
        win.control->set_unresponsive(true);
    }
}

template<typename Win>
void handle_ping_timeout(Win& win, uint32_t serial)
{
    auto it = win.pings.find(serial);
    if (it != win.pings.end()) {
        if (it->second == Win::ping_reason::close) {
            qCDebug(KWIN_CORE) << "Final ping timeout on a close attempt, asking to kill:"
                               << caption(&win);

            // for internal windows, killing the window will delete this
            QPointer<QObject> guard(win.qobject.get());
            win.killWindow();
            if (!guard) {
                return;
            }
        }
        win.pings.erase(it);
    }
}

template<typename Win>
void handle_pong(Win& win, uint32_t serial)
{
    auto it = win.pings.find(serial);
    if (it != win.pings.end()) {
        win.control->set_unresponsive(false);
        win.pings.erase(it);
    }
}

}
