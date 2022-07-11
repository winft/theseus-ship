/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "space.h"

#include "activation.h"
#include "active_window.h"
#include "deco/bridge.h"
#include "desktop_space.h"
#include "output_space.h"
#include "session.h"
#include "singleton_interface.h"
#include "space_areas_helpers.h"
#include "window_area.h"
#include "x11/tool_windows.h"

#include "base/dbus/kwin.h"
#include "base/output_helpers.h"
#include "base/x11/user_interaction_filter.h"
#include "base/x11/xcb/extensions.h"
#include "input/cursor.h"
#include "main.h"
#include "render/effects.h"
#include "render/outline.h"
#include "render/platform.h"
#include "render/post/night_color_manager.h"
#include "rules/rule_book.h"
#include "rules/rules.h"
#include "scripting/platform.h"
#include "utils/blocker.h"
#include "win/controlling.h"
#include "win/dbus/appmenu.h"
#include "win/dbus/virtual_desktop_manager.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/kill_window.h"
#include "win/layers.h"
#include "win/remnant.h"
#include "win/screen_edges.h"
#include "win/setup.h"
#include "win/shortcut_dialog.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/user_actions_menu.h"
#include "win/util.h"
#include "win/virtual_desktops.h"
#include "win/x11/control.h"
#include "win/x11/event.h"
#include "win/x11/group.h"
#include "win/x11/moving_window_filter.h"
#include "win/x11/netinfo.h"
#include "win/x11/space_areas.h"
#include "win/x11/space_setup.h"
#include "win/x11/stacking.h"
#include "win/x11/sync_alarm_filter.h"
#include "win/x11/transient.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window.h"

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

// TODO(romangg): For now this needs to be included late because of some conflict with Qt libraries.
#include "space_reconfigure.h"

#include <KStartupInfo>
#include <QAction>
#include <QtConcurrentRun>
#include <cassert>
#include <memory>

namespace KWin::win
{

space_qobject::space_qobject(std::function<void()> reconfigure_callback)
    : reconfigure_callback{reconfigure_callback}
{
}

void space_qobject::reconfigure()
{
    reconfigure_callback();
}

space::space(render::compositor& render)
    : qobject{std::make_unique<space_qobject>([this] { space_start_reconfigure_timer(*this); })}
    , outline{std::make_unique<render::outline>(render)}
    , render{render}
    , deco{std::make_unique<deco::bridge<space>>(*this)}
    , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
    , rule_book{std::make_unique<RuleBook>()}
    , user_actions_menu{std::make_unique<win::user_actions_menu<space>>(*this)}
    , stacking_order{std::make_unique<win::stacking_order>()}
    , focus_chain{win::focus_chain<space>(*this)}
    , virtual_desktop_manager{std::make_unique<win::virtual_desktop_manager>()}
    , dbus{std::make_unique<base::dbus::kwin_impl<space>>(*this)}
    , session_manager{std::make_unique<win::session_manager>()}
{
    // For invoke methods of user_actions_menu.
    qRegisterMetaType<Toplevel*>();

    singleton_interface::space = this;

    m_quickTileCombineTimer = new QTimer(qobject.get());
    m_quickTileCombineTimer->setSingleShot(true);

    init_rule_book(*rule_book, *this);

    // dbus interface
    new win::dbus::virtual_desktop_manager(virtual_desktop_manager.get());

#if KWIN_BUILD_TABBOX
    // need to create the tabbox before compositing scene is setup
    tabbox = std::make_unique<win::tabbox>(*this);
#endif

    QObject::connect(qobject.get(),
                     &space_qobject::currentDesktopChanged,
                     &render,
                     &render::compositor::addRepaintFull);

    deco->init();
    QObject::connect(qobject.get(), &space_qobject::configChanged, deco->qobject.get(), [this] {
        deco->reconfigure();
    });

    QObject::connect(session_manager.get(),
                     &win::session_manager::loadSessionRequested,
                     qobject.get(),
                     [this](auto&& session_name) { load_session_info(*this, session_name); });
    QObject::connect(
        session_manager.get(),
        &win::session_manager::prepareSessionSaveRequested,
        qobject.get(),
        [this](const QString& name) { store_session(*this, name, win::sm_save_phase0); });
    QObject::connect(
        session_manager.get(),
        &win::session_manager::finishSessionSaveRequested,
        qobject.get(),
        [this](const QString& name) { store_session(*this, name, win::sm_save_phase2); });

    auto& base = kwinApp()->get_base();
    QObject::connect(
        &base, &base::platform::topology_changed, qobject.get(), [this](auto old, auto topo) {
            if (old.size != topo.size) {
                desktopResized();
            }
        });

    QObject::connect(qobject.get(), &qobject_t::clientRemoved, qobject.get(), [this](auto window) {
        focus_chain_remove(focus_chain, window);
    });
    QObject::connect(qobject.get(),
                     &qobject_t::clientActivated,
                     qobject.get(),
                     [this](auto window) { focus_chain.active_window = window; });
    QObject::connect(virtual_desktop_manager.get(),
                     &win::virtual_desktop_manager::countChanged,
                     qobject.get(),
                     [this](auto prev, auto next) { focus_chain_resize(focus_chain, prev, next); });
    QObject::connect(virtual_desktop_manager.get(),
                     &win::virtual_desktop_manager::currentChanged,
                     qobject.get(),
                     [this](auto /*prev*/, auto next) { focus_chain.current_desktop = next; });
    QObject::connect(kwinApp()->options.get(),
                     &base::options::separateScreenFocusChanged,
                     qobject.get(),
                     [this](auto enable) { focus_chain.has_separate_screen_focus = enable; });
    focus_chain.has_separate_screen_focus = kwinApp()->options->isSeparateScreenFocus();

    auto vds = virtual_desktop_manager.get();
    QObject::connect(
        vds,
        &win::virtual_desktop_manager::countChanged,
        qobject.get(),
        [this](auto prev, auto next) { handle_desktop_count_changed(*this, prev, next); });
    QObject::connect(
        vds,
        &win::virtual_desktop_manager::currentChanged,
        qobject.get(),
        [this](auto prev, auto next) { handle_current_desktop_changed(*this, prev, next); });
    vds->setNavigationWrappingAround(kwinApp()->options->isRollOverDesktops());
    QObject::connect(kwinApp()->options.get(),
                     &base::options::rollOverDesktopsChanged,
                     vds,
                     &win::virtual_desktop_manager::setNavigationWrappingAround);

    auto config = kwinApp()->config();
    vds->setConfig(config);

    // positioning object needs to be created before the virtual desktops are loaded.
    vds->load();
    vds->updateLayout();

    // makes sure any autogenerated id is saved, necessary as in case of xwayland, load will be
    // called 2 times
    // load is needed to be called again when starting xwayalnd to sync to RootInfo, see BUG 385260
    vds->save();

    if (!vds->setCurrent(m_initialDesktop)) {
        vds->setCurrent(1);
    }

    reconfigureTimer.setSingleShot(true);
    updateToolWindowsTimer.setSingleShot(true);

    QObject::connect(
        &reconfigureTimer, &QTimer::timeout, qobject.get(), [this] { space_reconfigure(*this); });
    QObject::connect(&updateToolWindowsTimer, &QTimer::timeout, qobject.get(), [this] {
        x11::update_tool_windows_visibility(this, true);
    });

    // TODO: do we really need to reconfigure everything when fonts change?
    // maybe just reconfigure the decorations? Move this into libkdecoration?
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KDEPlatformTheme"),
                                          QStringLiteral("org.kde.KDEPlatformTheme"),
                                          QStringLiteral("refreshFonts"),
                                          qobject.get(),
                                          SLOT(reconfigure()));

    active_client = nullptr;
    QObject::connect(
        stacking_order.get(), &stacking_order::changed, qobject.get(), [this](auto count_changed) {
            x11::propagate_clients(*this, count_changed);
            if (active_client) {
                active_client->control->update_mouse_grab();
            }
        });
    QObject::connect(stacking_order.get(), &stacking_order::render_restack, qobject.get(), [this] {
        x11::render_stack_unmanaged_windows(*this);
    });
}

space::~space()
{
    stacking_order->lock();

    // TODO: grabXServer();

    win::x11::clear_space(*this);

    for (auto const& window : m_windows) {
        if (auto internal = qobject_cast<win::internal_window*>(window);
            internal && !internal->remnant) {
            internal->destroyClient();
            remove_all(m_windows, internal);
        }
    }

    // At this point only remnants are remaining.
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        assert((*it)->remnant);
        Q_EMIT qobject->window_deleted(*it);
        it = m_windows.erase(it);
    }

    assert(m_windows.empty());

    stacking_order.reset();

    rule_book.reset();
    kwinApp()->config()->sync();

    win::x11::root_info::destroy();
    delete startup;
    delete client_keys_dialog;
    for (auto const& s : session)
        delete s;

    // TODO: ungrabXServer();

    base::x11::xcb::extensions::destroy();
    singleton_interface::space = nullptr;
}

bool space::checkStartupNotification(xcb_window_t w, KStartupInfoId& id, KStartupInfoData& data)
{
    return startup->checkStartup(w, id, data) == KStartupInfo::Match;
}

void space::disableGlobalShortcutsForClient(bool disable)
{
    if (global_shortcuts_disabled_for_client == disable)
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                          QStringLiteral("/kglobalaccel"),
                                                          QStringLiteral("org.kde.KGlobalAccel"),
                                                          QStringLiteral("blockGlobalShortcuts"));
    message.setArguments(QList<QVariant>() << disable);
    QDBusConnection::sessionBus().asyncCall(message);

    global_shortcuts_disabled_for_client = disable;
    // Update also Meta+LMB actions etc.
    for (auto window : m_windows) {
        if (auto& ctrl = window->control) {
            ctrl->update_mouse_grab();
        }
    }
}

bool space::compositing() const
{
    return static_cast<bool>(render.scene);
}

void space::setWasUserInteraction()
{
    if (was_user_interaction) {
        return;
    }
    was_user_interaction = true;
    // might be called from within the filter, so delay till we now the filter returned
    QTimer::singleShot(0, qobject.get(), [this] { m_wasUserInteractionFilter.reset(); });
}

win::screen_edge* space::create_screen_edge(win::screen_edger& edger)
{
    return new win::screen_edge(&edger);
}

void space::updateTabbox()
{
#if KWIN_BUILD_TABBOX
    if (tabbox->is_displayed()) {
        tabbox->reset(true);
    }
#endif
}

QRect space::get_icon_geometry(Toplevel const* /*win*/) const
{
    return QRect();
}

void space::updateMinimizedOfTransients(Toplevel* c)
{
    // if mainwindow is minimized or shaded, minimize transients too
    auto const transients = c->transient()->children;

    if (c->control->minimized()) {
        for (auto it = transients.cbegin(); it != transients.cend(); ++it) {
            auto abstract_client = *it;
            if (abstract_client->transient()->modal())
                continue; // there's no reason to hide modal dialogs with the main client
            if (!(*it)->control) {
                continue;
            }
            // but to keep them to eg. watch progress or whatever
            if (!(*it)->control->minimized()) {
                win::set_minimized(abstract_client, true);
                updateMinimizedOfTransients(abstract_client);
            }
        }
        if (c->transient()
                ->modal()) { // if a modal dialog is minimized, minimize its mainwindow too
            for (auto c2 : c->transient()->leads()) {
                win::set_minimized(c2, true);
            }
        }
    } else {
        // else unmiminize the transients
        for (auto it = transients.cbegin(); it != transients.cend(); ++it) {
            auto abstract_client = *it;
            if (!(*it)->control) {
                continue;
            }
            if ((*it)->control->minimized()) {
                win::set_minimized(abstract_client, false);
                updateMinimizedOfTransients(abstract_client);
            }
        }
        if (c->transient()->modal()) {
            for (auto c2 : c->transient()->leads()) {
                win::set_minimized(c2, false);
            }
        }
    }
}

/**
 * Sets the client \a c's transient windows' on_all_desktops property to \a on_all_desktops.
 */
void space::updateOnAllDesktopsOfTransients(Toplevel* window)
{
    auto const transients = window->transient()->children;
    for (auto const& transient : transients) {
        if (transient->isOnAllDesktops() != window->isOnAllDesktops()) {
            win::set_on_all_desktops(transient, window->isOnAllDesktops());
        }
    }
}

// A new window has been mapped. Check if it's not a mainwindow for some already existing transient
// window.
void space::checkTransients(Toplevel* window)
{
    std::for_each(m_windows.cbegin(), m_windows.cend(), [&window](auto const& client) {
        client->checkTransient(window);
    });
}

/**
 * Resizes the space:: after an XRANDR screen size change
 */
void space::desktopResized()
{
    auto geom = QRect({}, kwinApp()->get_base().topology.size);
    if (win::x11::rootInfo()) {
        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        win::x11::rootInfo()->setDesktopGeometry(desktop_geometry);
    }

    update_space_areas(*this);

    // after updateClientArea(), so that one still uses the previous one
    saveOldScreenSizes();

    // TODO: emit a signal instead and remove the deep function calls into edges and effects
    edges->recreateEdges();

    if (auto& effects = render.effects) {
        effects->desktopResized(geom.size());
    }
}

void space::saveOldScreenSizes()
{
    auto&& base = kwinApp()->get_base();
    auto const& outputs = base.get_outputs();

    olddisplaysize = base.topology.size;
    oldscreensizes.clear();

    for (auto output : outputs) {
        oldscreensizes.push_back(output->geometry());
    }
}

void space::update_space_area_from_windows(QRect const& /*desktop_area*/,
                                           std::vector<QRect> const& /*screens_geos*/,
                                           win::space_areas& /*areas*/)
{
    // Can't be pure virtual because the function might be called from the ctor.
}

/**
 * Client \a c is moved around to position \a pos. This gives the
 * space:: the opportunity to interveniate and to implement
 * snap-to-windows functionality.
 *
 * The parameter \a snapAdjust is a multiplier used to calculate the
 * effective snap zones. When 1.0, it means that the snap zones will be
 * used without change.
 */
QPoint
space::adjustClientPosition(Toplevel* window, QPoint pos, bool unrestricted, double snapAdjust)
{
    QSize borderSnapZone(kwinApp()->options->borderSnapZone(),
                         kwinApp()->options->borderSnapZone());
    QRect maxRect;
    auto guideMaximized = win::maximize_mode::restore;
    if (window->maximizeMode() != win::maximize_mode::restore) {
        maxRect = space_window_area(
            *this, MaximizeArea, pos + QRect(QPoint(), window->size()).center(), window->desktop());
        QRect geo = window->frameGeometry();
        if (flags(window->maximizeMode() & win::maximize_mode::horizontal)
            && (geo.x() == maxRect.left() || geo.right() == maxRect.right())) {
            guideMaximized |= win::maximize_mode::horizontal;
            borderSnapZone.setWidth(qMax(borderSnapZone.width() + 2, maxRect.width() / 16));
        }
        if (flags(window->maximizeMode() & win::maximize_mode::vertical)
            && (geo.y() == maxRect.top() || geo.bottom() == maxRect.bottom())) {
            guideMaximized |= win::maximize_mode::vertical;
            borderSnapZone.setHeight(qMax(borderSnapZone.height() + 2, maxRect.height() / 16));
        }
    }

    if (kwinApp()->options->windowSnapZone() || !borderSnapZone.isNull()
        || kwinApp()->options->centerSnapZone()) {
        auto const& outputs = kwinApp()->get_base().get_outputs();
        const bool sOWO = kwinApp()->options->isSnapOnlyWhenOverlapping();
        auto output
            = base::get_nearest_output(outputs, pos + QRect(QPoint(), window->size()).center());

        if (maxRect.isNull()) {
            maxRect = space_window_area(*this, MovementArea, output, window->desktop());
        }

        const int xmin = maxRect.left();
        const int xmax = maxRect.right() + 1; // desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom() + 1;

        const int cx(pos.x());
        const int cy(pos.y());
        const int cw(window->size().width());
        const int ch(window->size().height());
        const int rx(cx + cw);
        const int ry(cy + ch); // these don't change

        int nx(cx), ny(cy); // buffers
        int deltaX(xmax);
        int deltaY(ymax); // minimum distance to other clients

        int lx, ly, lrx, lry; // coords and size for the comparison client, l

        // border snap
        const int snapX = borderSnapZone.width() * snapAdjust; // snap trigger
        const int snapY = borderSnapZone.height() * snapAdjust;
        if (snapX || snapY) {
            auto geo = window->frameGeometry();
            auto frameMargins = win::frame_margins(window);

            // snap to titlebar / snap to window borders on inner screen edges
            if (frameMargins.left()
                && (flags(window->maximizeMode() & win::maximize_mode::horizontal)
                    || base::get_intersecting_outputs(
                           outputs,
                           geo.translated(maxRect.x() - (frameMargins.left() + geo.x()), 0))
                            .size()
                        > 1)) {
                frameMargins.setLeft(0);
            }
            if (frameMargins.right()
                && (flags(window->maximizeMode() & win::maximize_mode::horizontal)
                    || base::get_intersecting_outputs(
                           outputs,
                           geo.translated(maxRect.right() + frameMargins.right() - geo.right(), 0))
                            .size()
                        > 1)) {
                frameMargins.setRight(0);
            }
            if (frameMargins.top()) {
                frameMargins.setTop(0);
            }
            if (frameMargins.bottom()
                && (flags(window->maximizeMode() & win::maximize_mode::vertical)
                    || base::get_intersecting_outputs(
                           outputs,
                           geo.translated(0,
                                          maxRect.bottom() + frameMargins.bottom() - geo.bottom()))
                            .size()
                        > 1)) {
                frameMargins.setBottom(0);
            }
            if ((sOWO ? (cx < xmin) : true) && (qAbs(xmin - cx) < snapX)) {
                deltaX = xmin - cx;
                nx = xmin - frameMargins.left();
            }
            if ((sOWO ? (rx > xmax) : true) && (qAbs(rx - xmax) < snapX)
                && (qAbs(xmax - rx) < deltaX)) {
                deltaX = rx - xmax;
                nx = xmax - cw + frameMargins.right();
            }

            if ((sOWO ? (cy < ymin) : true) && (qAbs(ymin - cy) < snapY)) {
                deltaY = ymin - cy;
                ny = ymin - frameMargins.top();
            }
            if ((sOWO ? (ry > ymax) : true) && (qAbs(ry - ymax) < snapY)
                && (qAbs(ymax - ry) < deltaY)) {
                deltaY = ry - ymax;
                ny = ymax - ch + frameMargins.bottom();
            }
        }

        // windows snap
        int snap = kwinApp()->options->windowSnapZone() * snapAdjust;
        if (snap) {
            for (auto win : m_windows) {
                if (!win->control) {
                    continue;
                }

                if (win == window) {
                    continue;
                }
                if (win->control->minimized()) {
                    continue;
                }
                if (!win->isShown()) {
                    continue;
                }
                if (!win->isOnDesktop(window->desktop()) && !window->isOnDesktop(win->desktop())) {
                    // wrong virtual desktop
                    continue;
                }
                if (is_desktop(win) || is_splash(win)) {
                    continue;
                }

                lx = win->pos().x();
                ly = win->pos().y();
                lrx = lx + win->size().width();
                lry = ly + win->size().height();

                if (!flags(guideMaximized & win::maximize_mode::horizontal)
                    && (((cy <= lry) && (cy >= ly)) || ((ry >= ly) && (ry <= lry))
                        || ((cy <= ly) && (ry >= lry)))) {
                    if ((sOWO ? (cx < lrx) : true) && (qAbs(lrx - cx) < snap)
                        && (qAbs(lrx - cx) < deltaX)) {
                        deltaX = qAbs(lrx - cx);
                        nx = lrx;
                    }
                    if ((sOWO ? (rx > lx) : true) && (qAbs(rx - lx) < snap)
                        && (qAbs(rx - lx) < deltaX)) {
                        deltaX = qAbs(rx - lx);
                        nx = lx - cw;
                    }
                }

                if (!flags(guideMaximized & win::maximize_mode::vertical)
                    && (((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx))
                        || ((cx <= lx) && (rx >= lrx)))) {
                    if ((sOWO ? (cy < lry) : true) && (qAbs(lry - cy) < snap)
                        && (qAbs(lry - cy) < deltaY)) {
                        deltaY = qAbs(lry - cy);
                        ny = lry;
                    }
                    // if ( (qAbs( ry-ly ) < snap) && (qAbs( ry - ly ) < deltaY ))
                    if ((sOWO ? (ry > ly) : true) && (qAbs(ry - ly) < snap)
                        && (qAbs(ry - ly) < deltaY)) {
                        deltaY = qAbs(ry - ly);
                        ny = ly - ch;
                    }
                }

                // Corner snapping
                if (!flags(guideMaximized & win::maximize_mode::vertical)
                    && (nx == lrx || nx + cw == lx)) {
                    if ((sOWO ? (ry > lry) : true) && (qAbs(lry - ry) < snap)
                        && (qAbs(lry - ry) < deltaY)) {
                        deltaY = qAbs(lry - ry);
                        ny = lry - ch;
                    }
                    if ((sOWO ? (cy < ly) : true) && (qAbs(cy - ly) < snap)
                        && (qAbs(cy - ly) < deltaY)) {
                        deltaY = qAbs(cy - ly);
                        ny = ly;
                    }
                }
                if (!flags(guideMaximized & win::maximize_mode::horizontal)
                    && (ny == lry || ny + ch == ly)) {
                    if ((sOWO ? (rx > lrx) : true) && (qAbs(lrx - rx) < snap)
                        && (qAbs(lrx - rx) < deltaX)) {
                        deltaX = qAbs(lrx - rx);
                        nx = lrx - cw;
                    }
                    if ((sOWO ? (cx < lx) : true) && (qAbs(cx - lx) < snap)
                        && (qAbs(cx - lx) < deltaX)) {
                        deltaX = qAbs(cx - lx);
                        nx = lx;
                    }
                }
            }
        }

        // center snap
        snap = kwinApp()->options->centerSnapZone() * snapAdjust; // snap trigger
        if (snap) {
            int diffX = qAbs((xmin + xmax) / 2 - (cx + cw / 2));
            int diffY = qAbs((ymin + ymax) / 2 - (cy + ch / 2));
            if (diffX < snap && diffY < snap && diffX < deltaX && diffY < deltaY) {
                // Snap to center of screen
                nx = (xmin + xmax) / 2 - cw / 2;
                ny = (ymin + ymax) / 2 - ch / 2;
            } else if (kwinApp()->options->borderSnapZone()) {
                // Enhance border snap
                if ((nx == xmin || nx == xmax - cw) && diffY < snap && diffY < deltaY) {
                    // Snap to vertical center on screen edge
                    ny = (ymin + ymax) / 2 - ch / 2;
                } else if (((unrestricted ? ny == ymin : ny <= ymin) || ny == ymax - ch)
                           && diffX < snap && diffX < deltaX) {
                    // Snap to horizontal center on screen edge
                    nx = (xmin + xmax) / 2 - cw / 2;
                }
            }
        }

        pos = QPoint(nx, ny);
    }
    return pos;
}

QRect space::adjustClientSize(Toplevel* window, QRect moveResizeGeom, win::position mode)
{
    // adapted from adjustClientPosition on 29May2004
    // this function is called when resizing a window and will modify
    // the new dimensions to snap to other windows/borders if appropriate
    if (kwinApp()->options->windowSnapZone()
        || kwinApp()->options->borderSnapZone()) { // || kwinApp()->options->centerSnapZone )
        const bool sOWO = kwinApp()->options->isSnapOnlyWhenOverlapping();

        auto const maxRect = space_window_area(
            *this, MovementArea, QRect(QPoint(0, 0), window->size()).center(), window->desktop());
        const int xmin = maxRect.left();
        const int xmax = maxRect.right(); // desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom();

        const int cx(moveResizeGeom.left());
        const int cy(moveResizeGeom.top());
        const int rx(moveResizeGeom.right());
        const int ry(moveResizeGeom.bottom());

        int newcx(cx), newcy(cy); // buffers
        int newrx(rx), newry(ry);
        int deltaX(xmax);
        int deltaY(ymax); // minimum distance to other clients

        int lx, ly, lrx, lry; // coords and size for the comparison client, l

        // border snap
        int snap = kwinApp()->options->borderSnapZone(); // snap trigger
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);

            auto snap_border_top = [&] {
                if ((sOWO ? (newcy < ymin) : true) && (qAbs(ymin - newcy) < deltaY)) {
                    deltaY = qAbs(ymin - newcy);
                    newcy = ymin;
                }
            };

            auto snap_border_bottom = [&] {
                if ((sOWO ? (newry > ymax) : true) && (qAbs(ymax - newry) < deltaY)) {
                    deltaY = qAbs(ymax - newcy);
                    newry = ymax;
                }
            };

            auto snap_border_left = [&] {
                if ((sOWO ? (newcx < xmin) : true) && (qAbs(xmin - newcx) < deltaX)) {
                    deltaX = qAbs(xmin - newcx);
                    newcx = xmin;
                }
            };

            auto snap_border_right = [&] {
                if ((sOWO ? (newrx > xmax) : true) && (qAbs(xmax - newrx) < deltaX)) {
                    deltaX = qAbs(xmax - newrx);
                    newrx = xmax;
                }
            };

            switch (mode) {
            case win::position::bottom_right:
                snap_border_bottom();
                snap_border_right();
                break;
            case win::position::right:
                snap_border_right();
                break;
            case win::position::bottom:
                snap_border_bottom();
                break;
            case win::position::top_left:
                snap_border_top();
                snap_border_left();
                break;
            case win::position::left:
                snap_border_left();
                break;
            case win::position::top:
                snap_border_top();
                break;
            case win::position::top_right:
                snap_border_top();
                snap_border_right();
                break;
            case win::position::bottom_left:
                snap_border_bottom();
                snap_border_left();
                break;
            default:
                abort();
                break;
            }
        }

        // windows snap
        snap = kwinApp()->options->windowSnapZone();
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);
            for (auto win : m_windows) {
                if (win->control && win->isOnDesktop(virtual_desktop_manager->current())
                    && !win->control->minimized() && win != window) {
                    lx = win->pos().x() - 1;
                    ly = win->pos().y() - 1;
                    lrx = win->pos().x() + win->size().width();
                    lry = win->pos().y() + win->size().height();

                    auto within_height = [&] {
                        return ((newcy <= lry) && (newcy >= ly))
                            || ((newry >= ly) && (newry <= lry))
                            || ((newcy <= ly) && (newry >= lry));
                    };
                    auto within_width = [&] {
                        return ((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx))
                            || ((cx <= lx) && (rx >= lrx));
                    };

                    auto snap_window_top = [&] {
                        if ((sOWO ? (newcy < lry) : true) && within_width()
                            && (qAbs(lry - newcy) < deltaY)) {
                            deltaY = qAbs(lry - newcy);
                            newcy = lry;
                        }
                    };
                    auto snap_window_bottom = [&] {
                        if ((sOWO ? (newry > ly) : true) && within_width()
                            && (qAbs(ly - newry) < deltaY)) {
                            deltaY = qAbs(ly - newry);
                            newry = ly;
                        }
                    };
                    auto snap_window_left = [&] {
                        if ((sOWO ? (newcx < lrx) : true) && within_height()
                            && (qAbs(lrx - newcx) < deltaX)) {
                            deltaX = qAbs(lrx - newcx);
                            newcx = lrx;
                        }
                    };
                    auto snap_window_right = [&] {
                        if ((sOWO ? (newrx > lx) : true) && within_height()
                            && (qAbs(lx - newrx) < deltaX)) {
                            deltaX = qAbs(lx - newrx);
                            newrx = lx;
                        }
                    };
                    auto snap_window_c_top = [&] {
                        if ((sOWO ? (newcy < ly) : true) && (newcx == lrx || newrx == lx)
                            && qAbs(ly - newcy) < deltaY) {
                            deltaY = qAbs(ly - newcy + 1);
                            newcy = ly + 1;
                        }
                    };
                    auto snap_window_c_bottom = [&] {
                        if ((sOWO ? (newry > lry) : true) && (newcx == lrx || newrx == lx)
                            && qAbs(lry - newry) < deltaY) {
                            deltaY = qAbs(lry - newry - 1);
                            newry = lry - 1;
                        }
                    };
                    auto snap_window_c_left = [&] {
                        if ((sOWO ? (newcx < lx) : true) && (newcy == lry || newry == ly)
                            && qAbs(lx - newcx) < deltaX) {
                            deltaX = qAbs(lx - newcx + 1);
                            newcx = lx + 1;
                        }
                    };
                    auto snap_window_c_right = [&] {
                        if ((sOWO ? (newrx > lrx) : true) && (newcy == lry || newry == ly)
                            && qAbs(lrx - newrx) < deltaX) {
                            deltaX = qAbs(lrx - newrx - 1);
                            newrx = lrx - 1;
                        }
                    };

                    switch (mode) {
                    case win::position::bottom_right:
                        snap_window_bottom();
                        snap_window_right();
                        snap_window_c_bottom();
                        snap_window_c_right();
                        break;
                    case win::position::right:
                        snap_window_right();
                        snap_window_c_right();
                        break;
                    case win::position::bottom:
                        snap_window_bottom();
                        snap_window_c_bottom();
                        break;
                    case win::position::top_left:
                        snap_window_top();
                        snap_window_left();
                        snap_window_c_top();
                        snap_window_c_left();
                        break;
                    case win::position::left:
                        snap_window_left();
                        snap_window_c_left();
                        break;
                    case win::position::top:
                        snap_window_top();
                        snap_window_c_top();
                        break;
                    case win::position::top_right:
                        snap_window_top();
                        snap_window_right();
                        snap_window_c_top();
                        snap_window_c_right();
                        break;
                    case win::position::bottom_left:
                        snap_window_bottom();
                        snap_window_left();
                        snap_window_c_bottom();
                        snap_window_c_left();
                        break;
                    default:
                        abort();
                        break;
                    }
                }
            }
        }

        // center snap
        // snap = kwinApp()->options->centerSnapZone;
        // if (snap)
        //    {
        //    // Don't resize snap to center as it interferes too much
        //    // There are two ways of implementing this if wanted:
        //    // 1) Snap only to the same points that the move snap does, and
        //    // 2) Snap to the horizontal and vertical center lines of the screen
        //    }

        moveResizeGeom = QRect(QPoint(newcx, newcy), QPoint(newrx, newry));
    }
    return moveResizeGeom;
}

// When kwin crashes, windows will not be gravitated back to their original position
// and will remain offset by the size of the decoration. So when restarting, fix this
// (the property with the size of the frame remains on the window after the crash).
void space::fixPositionAfterCrash(xcb_window_t w, const xcb_get_geometry_reply_t* geometry)
{
    NETWinInfo i(connection(), w, rootWindow(), NET::WMFrameExtents, NET::Properties2());
    NETStrut frame = i.frameExtents();

    if (frame.left != 0 || frame.top != 0) {
        // left and top needed due to narrowing conversations restrictions in C++11
        const uint32_t left = frame.left;
        const uint32_t top = frame.top;
        const uint32_t values[] = {geometry->x - left, geometry->y - top};
        xcb_configure_window(connection(), w, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    }
}

std::vector<Toplevel*> const& space::windows() const
{
    return m_windows;
}

std::vector<Toplevel*> space::unmanagedList() const
{
    std::vector<Toplevel*> ret;
    for (auto const& window : m_windows) {
        if (window->xcb_window && !window->control && !window->remnant) {
            ret.push_back(window);
        }
    }
    return ret;
}

std::vector<Toplevel*> space::remnants() const
{
    std::vector<Toplevel*> ret;
    for (auto const& window : m_windows) {
        if (window->remnant) {
            ret.push_back(window);
        }
    }
    return ret;
}

/**
 * Informs the space:: that the client \a c has been hidden. If it
 * was the active client (or to-become the active client),
 * the space:: activates another one.
 *
 * @note @p c may already be destroyed.
 */
void space::clientHidden(Toplevel* window)
{
    Q_ASSERT(!window->isShown() || !window->isOnCurrentDesktop());
    activate_next_window(*this, window);
}

}
