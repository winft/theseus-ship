/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "win/controlling.h"
#include "win/meta.h"
#include "win/setup.h"
#include "win/transient.h"

#include "toplevel.h"

#include <Wrapland/Server/appmenu.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/xdg_decoration.h>
#include <Wrapland/Server/xdg_shell_popup.h>
#include <Wrapland/Server/xdg_shell_toplevel.h>

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

    QObject::connect(surface->client(), &WS::Client::disconnected, win, &window::destroy);
    QObject::connect(surface, &WS::Surface::resourceDestroyed, win, &window::destroy);

    win->id = waylandServer()->createWindowId(surface);
    win->shell_surface = shell_surface;

    auto xdg_shell = waylandServer()->xdgShell();
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

    update_shadow(win);
    QObject::connect(win->surface(), &Wrapland::Server::Surface::shadowChanged, win, [win] {
        update_shadow(win);
    });

    QObject::connect(
        waylandServer(), &WaylandServer::foreignTransientChanged, win, [win](WS::Surface* child) {
            if (child == win->surface()) {
                handle_parent_changed(win);
            }
        });

    auto handle_first_commit = [win] {
        QObject::disconnect(win->surface(), &WS::Surface::committed, win, nullptr);
        QObject::connect(win->surface(), &WS::Surface::committed, win, &window::handle_commit);

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

    setup_connections(win);

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

    if (waylandServer()->inputMethodConnection() == toplevel->surface()->surface()->client()) {
        win->window_type = NET::OnScreenDisplay;
    }

    QObject::connect(toplevel, &WS::XdgShellToplevel::resourceDestroyed, win, &window::destroy);
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

    set_desktop(win, VirtualDesktopManager::self()->current());
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

    QObject::connect(win, &window::needsRepaint, Compositor::self(), [win] {
        Compositor::self()->schedule_repaint(win);
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
    QObject::connect(popup, &WS::XdgShellPopup::resourceDestroyed, win, &window::destroy);

    finalize_shell_window_creation(win);
    return win;
}

template<typename Win>
void update_screen_edge(Win* win)
{
    using PSS = Wrapland::Server::PlasmaShellSurface;

    if (!ScreenEdges::self()) {
        return;
    }

    auto const& plasma_surface = win->plasma_shell_surface;

    if (!win->mapped || !plasma_surface || plasma_surface->role() != PSS::Role::Panel) {
        ScreenEdges::self()->reserve(win, ElectricNone);
        return;
    }

    auto const is_auto_hidden
        = plasma_surface->panelBehavior() == PSS::PanelBehavior::AutoHide && win->hidden;
    auto const can_get_covered
        = plasma_surface->panelBehavior() == PSS::PanelBehavior::WindowsCanCover;

    if (!is_auto_hidden && !can_get_covered) {
        // Simple case with space being reserved for the panel.
        ScreenEdges::self()->reserve(win, ElectricNone);
        return;
    }

    // We need an edge for the screen edge API, so figure out which edge the window borders.
    Qt::Edges edges;
    auto const geometry = win->frameGeometry();

    for (int i = 0; i < screens()->count(); i++) {
        auto const screen_geo = screens()->geometry(i);
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
    ScreenEdges::self()->reserve(win, border);
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
            win->hideClient(true);
            win->plasma_shell_surface->hideAutoHidingPanel();
            update_screen_edge(win);
        });
        QObject::connect(surface, &PSS::panelAutoHideShowRequested, win, [win] {
            win->hideClient(false);
            ScreenEdges::self()->reserve(win, ElectricNone);
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
        win->control->update_application_menu_service_name(address.serviceName);
        win->control->update_application_menu_object_path(address.objectPath);
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
    return states;
}

template<typename Win>
QRect popup_placement(Win const* win, QRect const& bounds)
{
    using XSS = Wrapland::Server::XdgShellSurface;

    QRect anchor_rect;
    QPoint offset;
    Qt::Edges anchor_edge;
    Qt::Edges gravity;

    XSS::ConstraintAdjustments adjustments;

    // Returns true if target is within bounds, optional edges argument states which side to check.
    auto in_bounds = [bounds](auto const& target,
                              Qt::Edges edges = Qt::LeftEdge | Qt::RightEdge | Qt::TopEdge
                                  | Qt::BottomEdge) -> bool {
        if (edges & Qt::LeftEdge && target.left() < bounds.left()) {
            return false;
        }
        if (edges & Qt::TopEdge && target.top() < bounds.top()) {
            return false;
        }
        if (edges & Qt::RightEdge && target.right() > bounds.right()) {
            return false;
        }
        if (edges & Qt::BottomEdge && target.bottom() > bounds.bottom()) {
            return false;
        }
        return true;
    };

    auto get_anchor = [](auto const& rect, Qt::Edges edge, Qt::Edges gravity, auto const& size) {
        QPoint pos;

        switch (edge & (Qt::LeftEdge | Qt::RightEdge)) {
        case Qt::LeftEdge:
            pos.setX(rect.x());
            break;
        case Qt::RightEdge:
            pos.setX(rect.x() + rect.width());
            break;
        default:
            pos.setX(qRound(rect.x() + rect.width() / 2.0));
        }

        switch (edge & (Qt::TopEdge | Qt::BottomEdge)) {
        case Qt::TopEdge:
            pos.setY(rect.y());
            break;
        case Qt::BottomEdge:
            pos.setY(rect.y() + rect.height());
            break;
        default:
            pos.setY(qRound(rect.y() + rect.height() / 2.0));
        }

        // calculate where the top left point of the popup will end up with the applied
        // gravity gravity indicates direction. i.e if gravitating towards the top the popup's
        // bottom edge will next to the anchor point
        QPoint pos_adjust;
        switch (gravity & (Qt::LeftEdge | Qt::RightEdge)) {
        case Qt::LeftEdge:
            pos_adjust.setX(-size.width());
            break;
        case Qt::RightEdge:
            pos_adjust.setX(0);
            break;
        default:
            pos_adjust.setX(qRound(-size.width() / 2.0));
        }
        switch (gravity & (Qt::TopEdge | Qt::BottomEdge)) {
        case Qt::TopEdge:
            pos_adjust.setY(-size.height());
            break;
        case Qt::BottomEdge:
            pos_adjust.setY(0);
            break;
        default:
            pos_adjust.setY(qRound(-size.height() / 2.0));
        }

        return pos + pos_adjust;
    };

    auto transient_lead = win->transient()->lead();
    assert(transient_lead);

    auto const parent_pos
        = transient_lead->pos() + QPoint(left_border(transient_lead), top_border(transient_lead));

    anchor_rect = win->popup->anchorRect();
    anchor_edge = win->popup->anchorEdge();

    gravity = win->popup->gravity();
    offset = win->popup->anchorOffset();
    adjustments = win->popup->constraintAdjustments();

    auto size = win->geometry_update.frame.isValid() ? win->geometry_update.frame.size()
                                                     : win->popup->initialSize();

    auto place
        = QRect(get_anchor(anchor_rect, anchor_edge, gravity, size) + offset + parent_pos, size);

    if (in_bounds(place)) {
        // Fits in the bounds so we're done.
        return place;
    }

    // Note: Currently Qt clients don't seem to set any constraint adjustments at all.

    if (adjustments & XSS::ConstraintAdjustment::FlipX) {
        if (!in_bounds(place, Qt::LeftEdge | Qt::RightEdge)) {
            // Flip both edges (if either bit is set, XOR both).
            auto flippedanchor_edge = anchor_edge;
            if (flippedanchor_edge & (Qt::LeftEdge | Qt::RightEdge)) {
                flippedanchor_edge ^= (Qt::LeftEdge | Qt::RightEdge);
            }
            auto flippedGravity = gravity;
            if (flippedGravity & (Qt::LeftEdge | Qt::RightEdge)) {
                flippedGravity ^= (Qt::LeftEdge | Qt::RightEdge);
            }
            auto flipped_place
                = QRect(get_anchor(anchor_rect, flippedanchor_edge, flippedGravity, size) + offset
                            + parent_pos,
                        size);

            // If it still doesn't fit continue with the unflipped version.
            if (in_bounds(flipped_place, Qt::LeftEdge | Qt::RightEdge)) {
                place.moveLeft(flipped_place.left());
            }
        }
    }
    if (adjustments & XSS::ConstraintAdjustment::SlideX) {
        if (!in_bounds(place, Qt::LeftEdge)) {
            place.moveLeft(bounds.left());
        }
        if (!in_bounds(place, Qt::RightEdge)) {
            place.moveRight(bounds.right());
        }
    }
    if (adjustments & XSS::ConstraintAdjustment::ResizeX) {
        auto unconstrained_place = place;

        if (!in_bounds(unconstrained_place, Qt::LeftEdge)) {
            unconstrained_place.setLeft(bounds.left());
        }
        if (!in_bounds(unconstrained_place, Qt::RightEdge)) {
            unconstrained_place.setRight(bounds.right());
        }

        if (unconstrained_place.isValid()) {
            place = unconstrained_place;
        }
    }

    if (adjustments & XSS::ConstraintAdjustment::FlipY) {
        if (!in_bounds(place, Qt::TopEdge | Qt::BottomEdge)) {
            // flip both edges (if either bit is set, XOR both)
            auto flippedanchor_edge = anchor_edge;
            if (flippedanchor_edge & (Qt::TopEdge | Qt::BottomEdge)) {
                flippedanchor_edge ^= (Qt::TopEdge | Qt::BottomEdge);
            }
            auto flippedGravity = gravity;
            if (flippedGravity & (Qt::TopEdge | Qt::BottomEdge)) {
                flippedGravity ^= (Qt::TopEdge | Qt::BottomEdge);
            }
            auto flipped_place
                = QRect(get_anchor(anchor_rect, flippedanchor_edge, flippedGravity, size) + offset
                            + parent_pos,
                        size);

            // if it still doesn't fit we should continue with the unflipped version
            if (in_bounds(flipped_place, Qt::TopEdge | Qt::BottomEdge)) {
                place.moveTop(flipped_place.top());
            }
        }
    }
    if (adjustments & XSS::ConstraintAdjustment::SlideY) {
        if (!in_bounds(place, Qt::TopEdge)) {
            place.moveTop(bounds.top());
        }
        if (!in_bounds(place, Qt::BottomEdge)) {
            place.moveBottom(bounds.bottom());
        }
    }
    if (adjustments & XSS::ConstraintAdjustment::ResizeY) {
        auto unconstrained_place = place;

        if (!in_bounds(unconstrained_place, Qt::TopEdge)) {
            unconstrained_place.setTop(bounds.top());
        }
        if (!in_bounds(unconstrained_place, Qt::BottomEdge)) {
            unconstrained_place.setBottom(bounds.bottom());
        }

        if (unconstrained_place.isValid()) {
            place = unconstrained_place;
        }
    }

    return place;
}

template<typename Win>
bool needs_configure(Win* win)
{
    if (win->plasma_shell_surface && !win->layer_surface) {
        // Only have explicit and global position and size updates.
        return false;
    }

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

    auto parent = waylandServer()->find_window(parent_surface);

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
void handle_move_request(Win* win,
                         [[maybe_unused]] Wrapland::Server::Seat* seat,
                         [[maybe_unused]] uint32_t serial)
{
    // FIXME: Check the seat and serial.
    win->performMouseCommand(Options::MouseMove, Cursor::pos());
}

template<typename Win>
void handle_resize_request(Win* win,
                           [[maybe_unused]] Wrapland::Server::Seat* seat,
                           [[maybe_unused]] quint32 serial,
                           Qt::Edges edges)
{
    // FIXME: Check the seat and serial.

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
    mov_res.offset = Cursor::pos() - win->pos();

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
    win->performMouseCommand(Options::MouseMinimize, Cursor::pos());
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
        qCDebug(KWIN_CORE) << "First ping timeout:" << caption(win);
        win->control->set_unresponsive(true);
    }
}

template<typename Win>
void handle_ping_timeout(Win* win, uint32_t serial)
{
    auto it = win->pings.find(serial);
    if (it != win->pings.end()) {
        if (it->second == window::ping_reason::close) {
            qCDebug(KWIN_CORE) << "Final ping timeout on a close attempt, asking to kill:"
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
