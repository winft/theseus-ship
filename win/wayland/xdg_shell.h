/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "popup_placement.h"
#include "space.h"
#include "window.h"
#include "window_release.h"

#include "win/controlling.h"
#include "win/input.h"
#include "win/meta.h"
#include "win/setup.h"
#include "win/transient.h"

#include "render/compositor.h"
#include "toplevel.h"
#include "wayland_logging.h"
#include "wayland_server.h"

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

namespace KWin::win::wayland
{

class xdg_shell_control : public control
{
public:
    xdg_shell_control(window* win)
        : control(win)
        , m_window{win}
    {
    }

    bool can_fullscreen() const override
    {
        if (!rules().checkFullScreen(true)) {
            return false;
        }
        return !is_special_window(m_window);
    }

private:
    window* m_window;
};

inline window* create_shell_window(Wrapland::Server::XdgShellSurface* shell_surface)
{
    namespace WS = Wrapland::Server;

    auto const surface = shell_surface->surface();

    auto win = new window(surface);
    block_geometry_updates(win, true);

    QObject::connect(
        surface->client(), &WS::Client::disconnected, win, [win] { destroy_window(win); });

    win->shell_surface = shell_surface;

    auto xdg_shell = waylandServer()->xdg_shell();
    QObject::connect(xdg_shell, &WS::XdgShell::pingDelayed, win, [win](auto serial) {
        handle_ping_delayed(win, serial);
    });
    QObject::connect(xdg_shell, &WS::XdgShell::pingTimeout, win, [win](auto serial) {
        handle_ping_timeout(win, serial);
    });
    QObject::connect(xdg_shell, &WS::XdgShell::pongReceived, win, [win](auto serial) {
        handle_pong(win, serial);
    });

    return win;
}

template<typename Win>
void handle_parent_changed(Win* win);
template<typename Win>
void handle_minimize_request(Win* win);
template<typename Win>
void handle_maximize_request(Win* win, bool maximized);
template<typename Win>
Wrapland::Server::XdgShellSurface::States xdg_surface_states(Win* win);

inline void finalize_shell_window_creation(window* win)
{
    namespace WS = Wrapland::Server;

    QObject::connect(
        waylandServer(), &WaylandServer::foreignTransientChanged, win, [win](WS::Surface* child) {
            if (child == win->surface()) {
                handle_parent_changed(win);
            }
        });

    auto handle_first_commit = [win] {
        QObject::disconnect(win->surface(), &WS::Surface::committed, win, nullptr);
        QObject::connect(win->surface(), &WS::Surface::committed, win, &window::handle_commit);

        update_shadow(win);
        QObject::connect(win->surface(), &Wrapland::Server::Surface::committed, win, [win] {
            if (win->surface()->state().updates & Wrapland::Server::surface_change::shadow) {
                update_shadow(win);
            }
        });

        handle_parent_changed(win);

        if (win->control) {
            // Window is an xdg-shell toplevel.
            win->must_place = !win->isInitialPositionSet();

            if (win->supportsWindowRules()) {
                auto const& ctrl = win->control;

                setup_rules(win, false);

                auto const original_geo = win->frameGeometry();
                auto const ruled_geo = ctrl->rules().checkGeometry(original_geo, true);

                if (original_geo != ruled_geo) {
                    win->setFrameGeometry(ruled_geo);
                }

                maximize(win, ctrl->rules().checkMaximize(win->geometry_update.max_mode, true));

                set_desktop(win, ctrl->rules().checkDesktop(win->desktop(), true));
                set_desktop_file_name(
                    win, ctrl->rules().checkDesktopFile(ctrl->desktop_file_name(), true).toUtf8());
                if (ctrl->rules().checkMinimize(ctrl->minimized(), true)) {
                    // No animation.
                    set_minimized(win, true, true);
                }
                set_skip_taskbar(win, ctrl->rules().checkSkipTaskbar(ctrl->skip_taskbar(), true));
                set_skip_pager(win, ctrl->rules().checkSkipPager(ctrl->skip_pager(), true));
                set_skip_switcher(win,
                                  ctrl->rules().checkSkipSwitcher(ctrl->skip_switcher(), true));
                set_keep_above(win, ctrl->rules().checkKeepAbove(ctrl->keep_above(), true));
                set_keep_below(win, ctrl->rules().checkKeepBelow(ctrl->keep_below(), true));
                set_shortcut(win, ctrl->rules().checkShortcut(ctrl->shortcut().toString(), true));
                win->updateColorScheme();

                // Don't place the client if its position is set by a rule.
                if (ctrl->rules().checkPosition(invalidPoint, true) != invalidPoint) {
                    win->must_place = false;
                }

                ctrl->discard_temporary_rules();

                // Remove Apply Now rules.
                RuleBook::self()->discardUsed(win, false);

                win->updateWindowRules(Rules::All);
            }

            if (win->geometry_update.max_mode != maximize_mode::restore
                || win->geometry_update.fullscreen) {
                win->must_place = false;
            }
        }

        block_geometry_updates(win, false);

        if (win->pending_configures.empty()) {
            // xdg-shell protocol stipulates a single configure event on first commit.
            win->configure_geometry(QRect(win->pos(), QSize(0, 0)));
        }

        win->initialized = true;
    };

    QObject::connect(win->surface(), &WS::Surface::committed, win, [handle_first_commit] {
        handle_first_commit();
    });
}

template<typename Win>
void update_icon(Win* win)
{
    QString const wayland_icon = QStringLiteral("wayland");
    auto const df_icon = icon_from_desktop_file(win);
    auto const icon = df_icon.isEmpty() ? wayland_icon : df_icon;
    if (icon == win->control->icon().name()) {
        return;
    }
    win->control->set_icon(QIcon::fromTheme(icon));
}

inline window* create_toplevel_window(Wrapland::Server::XdgShellToplevel* toplevel)
{
    namespace WS = Wrapland::Server;

    auto win = create_shell_window(toplevel->surface());
    win->toplevel = toplevel;

    win->control = std::unique_ptr<control>(new xdg_shell_control(win));
    win->control->setup_tabbox();
    win->control->setup_color_scheme();

    setup_window_control_connections(win);

    auto update_icon = [win] {
        QString const wayland_icon = QStringLiteral("wayland");
        auto const df_icon = icon_from_desktop_file(win);
        auto const icon = df_icon.isEmpty() ? wayland_icon : df_icon;
        if (icon != win->control->icon().name()) {
            win->control->set_icon(QIcon::fromTheme(icon));
        }
    };

    QObject::connect(win, &window::desktopFileNameChanged, win, update_icon);
    update_icon();

    QObject::connect(
        toplevel, &WS::XdgShellToplevel::resourceDestroyed, win, [win] { destroy_window(win); });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::configureAcknowledged,
                     win,
                     [win](auto serial) { handle_configure_ack(win, serial); });

    win->caption.normal = QString::fromStdString(toplevel->title()).simplified();
    QObject::connect(
        toplevel, &WS::XdgShellToplevel::titleChanged, win, &window::handle_title_changed);
    QTimer::singleShot(0, win, &window::updateCaption);

    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::moveRequested,
                     win,
                     [win](auto seat, auto serial) { handle_move_request(win, seat, serial); });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::resizeRequested,
                     win,
                     [win](auto seat, auto serial, auto edges) {
                         handle_resize_request(win, seat, serial, edges);
                     });

    // Determine resource name, inspired by ICCCM 4.1.2.5: the binary name of the invoked client.
    QFileInfo info{QString::fromStdString(toplevel->client()->executablePath())};
    QByteArray resourceName;
    if (info.exists()) {
        resourceName = info.fileName().toUtf8();
    }
    win->setResourceClass(resourceName, toplevel->appId().c_str());
    set_desktop_file_name(win, toplevel->appId().c_str());
    QObject::connect(
        toplevel, &WS::XdgShellToplevel::appIdChanged, win, &window::handle_class_changed);

    QObject::connect(toplevel, &WS::XdgShellToplevel::minimizeRequested, win, [win] {
        handle_minimize_request(win);
    });
    QObject::connect(toplevel, &WS::XdgShellToplevel::maximizedChanged, win, [win](auto maximized) {
        handle_maximize_request(win, maximized);
    });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::fullscreenChanged,
                     win,
                     [win](auto fullscreen, auto output) {
                         handle_fullscreen_request(win, fullscreen, output);
                     });
    QObject::connect(toplevel,
                     &WS::XdgShellToplevel::windowMenuRequested,
                     win,
                     [win](auto seat, auto serial, auto surface_pos) {
                         handle_window_menu_request(win, seat, serial, surface_pos);
                     });
    QObject::connect(toplevel, &WS::XdgShellToplevel::transientForChanged, win, [win] {
        handle_parent_changed(win);
    });

    auto configure = [win, toplevel] {
        if (win->closing) {
            return;
        }
        if (win->geometry_update.block) {
            return;
        }
        auto size = win->synced_geometry.window.size();
        toplevel->configure(xdg_surface_states(win), size);
    };
    QObject::connect(win, &Toplevel::activeChanged, win, configure);
    QObject::connect(win, &Toplevel::clientStartUserMovedResized, win, configure);
    QObject::connect(win, &Toplevel::clientFinishUserMovedResized, win, configure);

    set_desktop(win, virtual_desktop_manager::self()->current());
    set_color_scheme(win, QString());

    finalize_shell_window_creation(win);
    return win;
}

inline window* create_popup_window(Wrapland::Server::XdgShellPopup* popup)
{
    namespace WS = Wrapland::Server;

    auto win = create_shell_window(popup->surface());
    win->popup = popup;
    win->transient()->annexed = true;

    QObject::connect(win, &window::needsRepaint, render::compositor::self(), [win] {
        render::compositor::self()->schedule_repaint(win);
    });
    QObject::connect(win, &window::frame_geometry_changed, win, [](auto win, auto old_frame_geo) {
        auto const old_visible_geo = visible_rect(win, old_frame_geo);
        auto const visible_geo = visible_rect(win, win->frameGeometry());

        lead_of_annexed_transient(win)->addLayerRepaint(old_visible_geo.united(visible_geo));

        if (old_visible_geo.size() != visible_geo.size()) {
            win->discard_quads();
        }
    });
    QObject::connect(popup, &WS::XdgShellPopup::configureAcknowledged, win, [win](auto serial) {
        handle_configure_ack(win, serial);
    });
    QObject::connect(popup, &WS::XdgShellPopup::grabRequested, win, [win](auto seat, auto serial) {
        handle_grab_request(win, seat, serial);
    });
    QObject::connect(
        popup, &WS::XdgShellPopup::resourceDestroyed, win, [win] { destroy_window(win); });

    finalize_shell_window_creation(win);
    return win;
}

template<typename Win>
void update_screen_edge(Win* win)
{
    using PSS = Wrapland::Server::PlasmaShellSurface;

    if (!workspace()->edges) {
        return;
    }

    auto const& plasma_surface = win->plasma_shell_surface;

    if (!win->mapped || !plasma_surface || plasma_surface->role() != PSS::Role::Panel) {
        workspace()->edges->reserve(win, ElectricNone);
        return;
    }

    auto const is_auto_hidden
        = plasma_surface->panelBehavior() == PSS::PanelBehavior::AutoHide && win->hidden;
    auto const can_get_covered
        = plasma_surface->panelBehavior() == PSS::PanelBehavior::WindowsCanCover;

    if (!is_auto_hidden && !can_get_covered) {
        // Simple case with space being reserved for the panel.
        workspace()->edges->reserve(win, ElectricNone);
        return;
    }

    // We need an edge for the screen edge API, so figure out which edge the window borders.
    Qt::Edges edges;
    auto const geometry = win->frameGeometry();
    auto const& screens = kwinApp()->get_base().screens;

    for (int i = 0; i < screens.count(); i++) {
        auto const screen_geo = screens.geometry(i);
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
    workspace()->edges->reserve(win, border);
}

template<typename Win>
void install_plasma_shell_surface(Win* win, Wrapland::Server::PlasmaShellSurface* surface)
{
    using PSS = Wrapland::Server::PlasmaShellSurface;

    win->plasma_shell_surface = surface;

    QObject::connect(
        surface, &PSS::resourceDestroyed, win, [win] { win->plasma_shell_surface = nullptr; });

    auto update_position = [win, surface] {
        win->setFrameGeometry(QRect(surface->position(), win->geometry_update.frame.size()));
    };
    auto update_role = [win, surface] {
        auto type = NET::Unknown;
        switch (surface->role()) {
        case PSS::Role::Desktop:
            type = NET::Desktop;
            break;
        case PSS::Role::Panel:
            type = NET::Dock;
            break;
        case PSS::Role::OnScreenDisplay:
            type = NET::OnScreenDisplay;
            break;
        case PSS::Role::Notification:
            type = NET::Notification;
            break;
        case PSS::Role::ToolTip:
            type = NET::Tooltip;
            break;
        case PSS::Role::CriticalNotification:
            type = NET::CriticalNotification;
            break;
        case PSS::Role::Normal:
        default:
            type = NET::Normal;
            break;
        }
        if (type != win->window_type) {
            win->window_type = type;
            if (type == NET::Desktop || type == NET::Dock || type == NET::OnScreenDisplay
                || type == NET::Notification || type == NET::Tooltip
                || type == NET::CriticalNotification) {
                set_on_all_desktops(win, true);
            }
            workspace()->updateClientArea();
        }
    };

    win->must_place = false;
    update_role();
    update_screen_edge(win);

    if (surface->isPositionSet()) {
        update_position();
    }

    QObject::connect(surface, &PSS::positionChanged, win, update_position);
    QObject::connect(surface, &PSS::roleChanged, win, update_role);
    QObject::connect(surface, &PSS::panelBehaviorChanged, win, [win] {
        update_screen_edge(win);
        workspace()->updateClientArea();
    });
    QObject::connect(win, &window::frame_geometry_changed, win, [win] { update_screen_edge(win); });

    if (win->control) {
        QObject::connect(surface, &PSS::panelAutoHideHideRequested, win, [win] {
            if (win->plasma_shell_surface->panelBehavior() == PSS::PanelBehavior::AutoHide) {
                win->hideClient(true);
                win->plasma_shell_surface->hideAutoHidingPanel();
            }
            update_screen_edge(win);
        });
        QObject::connect(surface, &PSS::panelAutoHideShowRequested, win, [win] {
            win->hideClient(false);
            workspace()->edges->reserve(win, ElectricNone);
            win->plasma_shell_surface->showAutoHidingPanel();
        });

        win::set_skip_taskbar(win, surface->skipTaskbar());
        win::set_skip_switcher(win, surface->skipSwitcher());

        QObject::connect(surface, &PSS::skipTaskbarChanged, win, [win] {
            win::set_skip_taskbar(win, win->plasma_shell_surface->skipTaskbar());
        });
        QObject::connect(surface, &PSS::skipSwitcherChanged, win, [win] {
            win::set_skip_switcher(win, win->plasma_shell_surface->skipSwitcher());
        });
    }
}

template<typename Win>
void install_appmenu(Win* win, Wrapland::Server::Appmenu* menu)
{
    using Menu = Wrapland::Server::Appmenu;

    auto update = [win](Menu::InterfaceAddress address) {
        win->control->update_application_menu({address.serviceName, address.objectPath});
    };

    QObject::connect(menu, &Menu::addressChanged, win, [update](Menu::InterfaceAddress address) {
        update(address);
    });
    update(menu->address());
}

template<typename Win>
void install_palette(Win* win, Wrapland::Server::ServerSideDecorationPalette* palette)
{
    using Palette = Wrapland::Server::ServerSideDecorationPalette;

    win->palette = palette;

    auto update = [win](auto const& palette) {
        set_color_scheme(win, win->control->rules().checkDecoColor(palette));
    };

    QObject::connect(palette, &Palette::paletteChanged, win, [update](auto name) { update(name); });
    QObject::connect(palette, &QObject::destroyed, win, [update] { update(QString()); });

    update(palette->palette());
}

template<typename Win>
void install_deco(Win* win, Wrapland::Server::XdgDecoration* deco)
{
    using Deco = Wrapland::Server::XdgDecoration;

    assert(win->control);
    win->xdg_deco = deco;

    QObject::connect(deco, &Deco::resourceDestroyed, win, [win] {
        win->xdg_deco = nullptr;
        if (win->closing || !Workspace::self()) {
            return;
        }
        win->updateDecoration(true);
    });

    QObject::connect(deco, &Deco::modeRequested, win, [win]() {
        // Force as we must send a new configure response.
        win->updateDecoration(false, true);
    });
}

template<typename Window, typename Space>
void handle_new_toplevel(Space* space, Wrapland::Server::XdgShellToplevel* toplevel)
{
    if (!Workspace::self()) {
        // it's possible that a Surface gets created before Workspace is created
        // TODO(romangg): Make this check unnecessary.
        return;
    }
    if (toplevel->client() == space->server->screenLockerClientConnection()) {
        ScreenLocker::KSldApp::self()->lockScreenShown();
    }
    auto window = win::wayland::create_toplevel_window(toplevel);

    // TODO(romangg): Also relevant for popups?
    auto it = std::find_if(
        space->plasma_shell_surfaces.begin(),
        space->plasma_shell_surfaces.end(),
        [window](auto shell_surface) { return window->surface() == shell_surface->surface(); });
    if (it != space->plasma_shell_surfaces.end()) {
        win::wayland::install_plasma_shell_surface(window, *it);
        space->plasma_shell_surfaces.erase(it);
    }

    if (auto menu = space->server->globals->appmenu_manager->appmenuForSurface(window->surface())) {
        win::wayland::install_appmenu(window, menu);
    }
    if (auto palette
        = space->server->globals->server_side_decoration_palette_manager->paletteForSurface(
            toplevel->surface()->surface())) {
        win::wayland::install_palette(window, palette);
    }

    space->m_windows.push_back(window);

    if (window->readyForPainting()) {
        space->handle_window_added(window);
    } else {
        QObject::connect(
            window, &win::wayland::window::windowShown, space, &Space::handle_wayland_window_shown);
    }

    // Not directly connected as the connection is tied to client instead of this.
    // TODO(romangg): What does this mean?
    QObject::connect(space->server->globals->xdg_foreign.get(),
                     &Wrapland::Server::XdgForeign::parentChanged,
                     window,
                     [server = space->server](auto /*parent*/, auto child) {
                         Q_EMIT server->foreignTransientChanged(child);
                     });
}

template<typename Window, typename Space>
void handle_new_popup(Space* space, Wrapland::Server::XdgShellPopup* popup)
{
    if (!Workspace::self()) {
        // it's possible that a Surface gets created before Workspace is created
        // TODO(romangg): Make this check unnecessary.
        return;
    }

    auto window = win::wayland::create_popup_window(popup);
    space->m_windows.push_back(window);

    if (window->readyForPainting()) {
        space->handle_window_added(window);
    } else {
        QObject::connect(
            window, &win::wayland::window::windowShown, space, &Space::handle_wayland_window_shown);
    }
}

template<typename Win>
Wrapland::Server::XdgShellSurface::States xdg_surface_states(Win* win)
{
    using XSS = Wrapland::Server::XdgShellSurface;

    XSS::States states;

    if (win->control->active()) {
        states |= XSS::State::Activated;
    }
    if (win->synced_geometry.fullscreen) {
        states |= XSS::State::Fullscreen;
    }
    if (win->synced_geometry.max_mode == win::maximize_mode::full) {
        states |= XSS::State::Maximized;
    }
    if (is_resize(win)) {
        states |= XSS::State::Resizing;
    }
    if (flags(win->control->quicktiling() & quicktiles::left)) {
        states |= XSS::State::TiledLeft;
    }
    if (flags(win->control->quicktiling() & quicktiles::right)) {
        states |= XSS::State::TiledRight;
    }
    if (flags(win->control->quicktiling() & quicktiles::top)) {
        states |= XSS::State::TiledTop;
    }
    if (flags(win->control->quicktiling() & quicktiles::bottom)) {
        states |= XSS::State::TiledBottom;
    }
    return states;
}

template<typename Win>
QRect get_xdg_shell_popup_placement(Win const* win, QRect const& bounds)
{
    // Note: Currently Qt clients don't seem to set any constraint adjustments at all.

    auto transient_lead = win->transient()->lead();
    assert(transient_lead);

    auto get = get_popup_placement<std::remove_pointer_t<decltype(transient_lead)>>;
    return get({transient_lead,
                bounds,
                win->popup->anchorRect(),
                win->popup->anchorEdge(),
                win->popup->gravity(),
                win->geometry_update.frame.isValid() ? win->geometry_update.frame.size()
                                                     : win->popup->initialSize(),
                win->popup->anchorOffset(),
                win->popup->constraintAdjustments()});
}

template<typename Win>
bool needs_configure(Win* win)
{
    auto const& update = win->geometry_update;

    if (update.max_mode != win->synced_geometry.max_mode) {
        return true;
    }
    if (update.fullscreen != win->synced_geometry.fullscreen) {
        return true;
    }

    auto ref_geo = update.frame - frame_margins(win);

    return ref_geo.isEmpty() || ref_geo.size() != win->synced_geometry.window.size();
}

template<typename Win>
void move_annexed_children(Win* win, QPoint const& frame_pos_offset)
{
    for (auto child : win->transient()->children) {
        if (!child->transient()->annexed) {
            continue;
        }
        auto pos = child->geometry_update.frame.topLeft() + frame_pos_offset;
        auto size = child->geometry_update.frame.size();
        child->setFrameGeometry(QRect(pos, size));
    }
}

template<typename Win>
void reposition_annexed_children(Win* win)
{
    // TODO(romangg): We currently don't yet have support for implicit or explicit popup
    //                repositioning introduced with xdg-shell v3.

    for (auto child : win->transient()->children) {
        if (!child->transient()->annexed) {
            continue;
        }
        auto wl_child = static_cast<window*>(child);
        if (wl_child->popup) {
            reposition_annexed_children(wl_child);
        }
    }

    // TODO(romangg): The popups should just be cancelled when there is no support for xdg-shell v3.
    //                But cancel_popup() is for some reason failing in Wrapland at the moment.
}

template<typename Win>
void handle_configure_ack(Win* win, uint32_t serial)
{
    win->acked_configure = serial;
}

template<typename Win>
void handle_parent_changed(Win* win)
{
    Wrapland::Server::Surface* parent_surface = nullptr;
    if (win->toplevel) {
        if (auto parent = win->toplevel->transientFor()) {
            parent_surface = parent->surface()->surface();
        }
    }
    if (win->popup) {
        parent_surface = win->popup->transientFor()->surface();
    }

    if (!parent_surface) {
        parent_surface = waylandServer()->findForeignParentForSurface(win->surface());
    }

    auto parent = static_cast<space*>(workspace())->find_window(parent_surface);

    if (auto lead = win->transient()->lead(); parent != lead) {
        // Remove from main client.
        if (lead) {
            lead->transient()->remove_child(win);
        }
        if (parent) {
            parent->transient()->add_child(win);
        }
    }
}

template<typename Win>
void handle_move_request(Win* win, Wrapland::Server::Seat* seat, uint32_t serial)
{
    if (!seat->pointers().has_implicit_grab(serial) && !seat->touches().has_implicit_grab(serial)) {
        return;
    }
    if (win->isMovable()) {
        win->performMouseCommand(Options::MouseMove, input::get_cursor()->pos());
    }
}

template<typename Win>
void handle_resize_request(Win* win, Wrapland::Server::Seat* seat, quint32 serial, Qt::Edges edges)
{
    if (!seat->pointers().has_implicit_grab(serial) && !seat->touches().has_implicit_grab(serial)) {
        return;
    }

    if (!win->isResizable()) {
        return;
    }
    if (win->control->move_resize().enabled) {
        finish_move_resize(win, false);
    }

    auto& mov_res = win->control->move_resize();
    mov_res.button_down = true;
    mov_res.unrestricted = false;

    // The offset describes the resize cursor contact position in frame geometry local coordinates,
    // i.e. with the origin in the top-left corner of the frame geometry.
    // Note that this might have negative coordinates if we resize by grabbing the shadow area of
    // the left or top edge.
    mov_res.offset = input::get_cursor()->pos() - win->pos();

    // The inverted offset describes the difference between bottom-right corner and offset.
    mov_res.inverted_offset
        = QPoint(win->size().width() - 1, win->size().height() - 1) - mov_res.offset;

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

    if (!start_move_resize(win)) {
        mov_res.button_down = false;
    }
    update_cursor(win);
}

template<typename Win>
void handle_minimize_request(Win* win)
{
    win->performMouseCommand(Options::MouseMinimize, input::get_cursor()->pos());
}

template<typename Win>
void handle_maximize_request(Win* win, bool maximized)
{
    auto const old_max_mode = win->geometry_update.max_mode;
    maximize(win, maximized ? maximize_mode::full : maximize_mode::restore);

    if (win->geometry_update.max_mode == old_max_mode) {
        // No change, still send a configure event with current geometry.
        auto sync_geo = win->synced_geometry.window;
        if (sync_geo.isValid()) {
            sync_geo += frame_margins(win);
        }
        win->configure_geometry(sync_geo);
    }
}

template<typename Win>
void handle_fullscreen_request(Win* win,
                               bool fullscreen,
                               [[maybe_unused]] Wrapland::Server::Output* output)
{
    // TODO: Consider output.
    win->setFullScreen(fullscreen, false);
}

template<typename Win>
void handle_window_menu_request(Win* win,
                                [[maybe_unused]] Wrapland::Server::Seat* seat,
                                [[maybe_unused]] quint32 serial,
                                QPoint const& surfacePos)
{
    win->performMouseCommand(Options::MouseOperationsMenu, win->pos() + surfacePos);
}

template<typename Win>
void handle_grab_request(Win* win,
                         [[maybe_unused]] Wrapland::Server::Seat* seat,
                         [[maybe_unused]] quint32 serial)
{
    // FIXME: Check the seat and serial as well whether the parent had focus.

    win->transient()->input_grab = true;
}

template<typename Win>
void handle_ping_delayed(Win* win, uint32_t serial)
{
    auto it = win->pings.find(serial);
    if (it != win->pings.end()) {
        qCDebug(KWIN_WL) << "First ping timeout:" << caption(win);
        win->control->set_unresponsive(true);
    }
}

template<typename Win>
void handle_ping_timeout(Win* win, uint32_t serial)
{
    auto it = win->pings.find(serial);
    if (it != win->pings.end()) {
        if (it->second == window::ping_reason::close) {
            qCDebug(KWIN_WL) << "Final ping timeout on a close attempt, asking to kill:"
                             << win::caption(win);

            // for internal windows, killing the window will delete this
            QPointer<QObject> guard(win);
            win->killWindow();
            if (!guard) {
                return;
            }
        }
        win->pings.erase(it);
    }
}

template<typename Win>
void handle_pong(Win* win, uint32_t serial)
{
    auto it = win->pings.find(serial);
    if (it != win->pings.end()) {
        win->control->set_unresponsive(false);
        win->pings.erase(it);
    }
}

}
