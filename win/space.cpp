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
#include "deco/bridge.h"
#include "desktop_space.h"
#include "output_space.h"
#include "singleton_interface.h"
#include "space_areas_helpers.h"
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

#include <KConfig>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <KProcess>
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
                     [this](auto&& session_name) { loadSessionInfo(session_name); });
    QObject::connect(session_manager.get(),
                     &win::session_manager::prepareSessionSaveRequested,
                     qobject.get(),
                     [this](const QString& name) { storeSession(name, win::sm_save_phase0); });
    QObject::connect(session_manager.get(),
                     &win::session_manager::finishSessionSaveRequested,
                     qobject.get(),
                     [this](const QString& name) { storeSession(name, win::sm_save_phase2); });

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

void space::setShowingDesktop(bool showing)
{
    const bool changed = showing != showing_desktop;
    if (win::x11::rootInfo() && changed) {
        win::x11::rootInfo()->setShowingDesktop(showing);
    }
    showing_desktop = showing;

    Toplevel* topDesk = nullptr;

    {                                  // for the blocker RAII
        blocker block(stacking_order); // updateLayer & lowerClient would invalidate stacking_order
        for (int i = static_cast<int>(stacking_order->stack.size()) - 1; i > -1; --i) {
            auto c = qobject_cast<Toplevel*>(stacking_order->stack.at(i));
            if (c && c->isOnCurrentDesktop()) {
                if (win::is_dock(c)) {
                    win::update_layer(c);
                } else if (win::is_desktop(c) && c->isShown()) {
                    win::update_layer(c);
                    win::lower_window(this, c);
                    if (!topDesk)
                        topDesk = c;
                    if (auto group = c->group()) {
                        for (auto cm : group->members) {
                            win::update_layer(cm);
                        }
                    }
                }
            }
        }
    } // ~Blocker

    if (showing_desktop && topDesk) {
        request_focus(*this, topDesk);
    } else if (!showing_desktop && changed) {
        const auto client = focus_chain_get_for_activation_on_current_output<Toplevel>(
            focus_chain, virtual_desktop_manager->current());
        if (client) {
            activate_window(*this, client);
        }
    }
    if (changed) {
        Q_EMIT qobject->showingDesktopChanged(showing);
    }
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
    saveOldScreenSizes(); // after updateClientArea(), so that one still uses the previous one

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
 * Returns the area available for clients. This is the desktop
 * geometry minus windows on the dock. Placement algorithms should
 * refer to this rather than Screens::geometry.
 */
QRect space::clientArea(clientAreaOption opt, base::output const* output, int desktop) const
{
    auto const& outputs = kwinApp()->get_base().get_outputs();

    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0)
        desktop = virtual_desktop_manager->current();
    if (!output) {
        output = get_current_output(*this);
    }

    QRect output_geo;
    size_t output_index{0};

    if (output) {
        output_geo = output->geometry();
        output_index = base::get_output_index(outputs, *output);
    }

    auto& base = kwinApp()->get_base();
    QRect sarea, warea;
    sarea = (!areas.screen.empty()
             // screens may be missing during KWin initialization or screen config changes
             && output_index < areas.screen[desktop].size())
        ? areas.screen[desktop][output_index]
        : output_geo;
    warea = areas.work[desktop].isNull() ? QRect({}, base.topology.size) : areas.work[desktop];

    switch (opt) {
    case MaximizeArea:
    case PlacementArea:
        return sarea;
    case MaximizeFullArea:
    case FullScreenArea:
    case MovementArea:
    case ScreenArea:
        return output_geo;
    case WorkArea:
        return warea;
    case FullArea:
        return QRect({}, base.topology.size);
    }
    abort();
}

QRect space::clientArea(clientAreaOption opt, const QPoint& p, int desktop) const
{
    return clientArea(
        opt, base::get_nearest_output(kwinApp()->get_base().get_outputs(), p), desktop);
}

QRect space::clientArea(clientAreaOption opt, Toplevel const* window) const
{
    return clientArea(opt, win::pending_frame_geometry(window).center(), window->desktop());
}

static QRegion strutsToRegion(win::space const& space,
                              int desktop,
                              win::strut_area areas,
                              std::vector<win::strut_rects> const& struts)
{
    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0) {
        desktop = space.virtual_desktop_manager->current();
    }

    QRegion region;
    auto const& rects = struts[desktop];

    for (auto const& rect : rects) {
        if (flags(areas & rect.area())) {
            region += rect;
        }
    }

    return region;
}

QRegion space::restrictedMoveArea(int desktop, win::strut_area areas) const
{
    return strutsToRegion(*this, desktop, areas, this->areas.restrictedmove);
}

bool space::inUpdateClientArea() const
{
    return !oldrestrictedmovearea.empty();
}

QRegion space::previousRestrictedMoveArea(int desktop, win::strut_area areas) const
{
    return strutsToRegion(*this, desktop, areas, oldrestrictedmovearea);
}

std::vector<QRect> space::previousScreenSizes() const
{
    return oldscreensizes;
}

int space::oldDisplayWidth() const
{
    return olddisplaysize.width();
}

int space::oldDisplayHeight() const
{
    return olddisplaysize.height();
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
        maxRect = clientArea(
            MaximizeArea, pos + QRect(QPoint(), window->size()).center(), window->desktop());
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
            maxRect = clientArea(MovementArea, output, window->desktop());
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

        auto const maxRect = clientArea(
            MovementArea, QRect(QPoint(0, 0), window->size()).center(), window->desktop());
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

#define SNAP_BORDER_TOP                                                                            \
    if ((sOWO ? (newcy < ymin) : true) && (qAbs(ymin - newcy) < deltaY)) {                         \
        deltaY = qAbs(ymin - newcy);                                                               \
        newcy = ymin;                                                                              \
    }

#define SNAP_BORDER_BOTTOM                                                                         \
    if ((sOWO ? (newry > ymax) : true) && (qAbs(ymax - newry) < deltaY)) {                         \
        deltaY = qAbs(ymax - newcy);                                                               \
        newry = ymax;                                                                              \
    }

#define SNAP_BORDER_LEFT                                                                           \
    if ((sOWO ? (newcx < xmin) : true) && (qAbs(xmin - newcx) < deltaX)) {                         \
        deltaX = qAbs(xmin - newcx);                                                               \
        newcx = xmin;                                                                              \
    }

#define SNAP_BORDER_RIGHT                                                                          \
    if ((sOWO ? (newrx > xmax) : true) && (qAbs(xmax - newrx) < deltaX)) {                         \
        deltaX = qAbs(xmax - newrx);                                                               \
        newrx = xmax;                                                                              \
    }
            switch (mode) {
            case win::position::bottom_right:
                SNAP_BORDER_BOTTOM
                SNAP_BORDER_RIGHT
                break;
            case win::position::right:
                SNAP_BORDER_RIGHT
                break;
            case win::position::bottom:
                SNAP_BORDER_BOTTOM
                break;
            case win::position::top_left:
                SNAP_BORDER_TOP
                SNAP_BORDER_LEFT
                break;
            case win::position::left:
                SNAP_BORDER_LEFT
                break;
            case win::position::top:
                SNAP_BORDER_TOP
                break;
            case win::position::top_right:
                SNAP_BORDER_TOP
                SNAP_BORDER_RIGHT
                break;
            case win::position::bottom_left:
                SNAP_BORDER_BOTTOM
                SNAP_BORDER_LEFT
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

#define WITHIN_HEIGHT                                                                              \
    (((newcy <= lry) && (newcy >= ly)) || ((newry >= ly) && (newry <= lry))                        \
     || ((newcy <= ly) && (newry >= lry)))

#define WITHIN_WIDTH                                                                               \
    (((cx <= lrx) && (cx >= lx)) || ((rx >= lx) && (rx <= lrx)) || ((cx <= lx) && (rx >= lrx)))

#define SNAP_WINDOW_TOP                                                                            \
    if ((sOWO ? (newcy < lry) : true) && WITHIN_WIDTH && (qAbs(lry - newcy) < deltaY)) {           \
        deltaY = qAbs(lry - newcy);                                                                \
        newcy = lry;                                                                               \
    }

#define SNAP_WINDOW_BOTTOM                                                                         \
    if ((sOWO ? (newry > ly) : true) && WITHIN_WIDTH && (qAbs(ly - newry) < deltaY)) {             \
        deltaY = qAbs(ly - newry);                                                                 \
        newry = ly;                                                                                \
    }

#define SNAP_WINDOW_LEFT                                                                           \
    if ((sOWO ? (newcx < lrx) : true) && WITHIN_HEIGHT && (qAbs(lrx - newcx) < deltaX)) {          \
        deltaX = qAbs(lrx - newcx);                                                                \
        newcx = lrx;                                                                               \
    }

#define SNAP_WINDOW_RIGHT                                                                          \
    if ((sOWO ? (newrx > lx) : true) && WITHIN_HEIGHT && (qAbs(lx - newrx) < deltaX)) {            \
        deltaX = qAbs(lx - newrx);                                                                 \
        newrx = lx;                                                                                \
    }

#define SNAP_WINDOW_C_TOP                                                                          \
    if ((sOWO ? (newcy < ly) : true) && (newcx == lrx || newrx == lx)                              \
        && qAbs(ly - newcy) < deltaY) {                                                            \
        deltaY = qAbs(ly - newcy + 1);                                                             \
        newcy = ly + 1;                                                                            \
    }

#define SNAP_WINDOW_C_BOTTOM                                                                       \
    if ((sOWO ? (newry > lry) : true) && (newcx == lrx || newrx == lx)                             \
        && qAbs(lry - newry) < deltaY) {                                                           \
        deltaY = qAbs(lry - newry - 1);                                                            \
        newry = lry - 1;                                                                           \
    }

#define SNAP_WINDOW_C_LEFT                                                                         \
    if ((sOWO ? (newcx < lx) : true) && (newcy == lry || newry == ly)                              \
        && qAbs(lx - newcx) < deltaX) {                                                            \
        deltaX = qAbs(lx - newcx + 1);                                                             \
        newcx = lx + 1;                                                                            \
    }

#define SNAP_WINDOW_C_RIGHT                                                                        \
    if ((sOWO ? (newrx > lrx) : true) && (newcy == lry || newry == ly)                             \
        && qAbs(lrx - newrx) < deltaX) {                                                           \
        deltaX = qAbs(lrx - newrx - 1);                                                            \
        newrx = lrx - 1;                                                                           \
    }

                    switch (mode) {
                    case win::position::bottom_right:
                        SNAP_WINDOW_BOTTOM
                        SNAP_WINDOW_RIGHT
                        SNAP_WINDOW_C_BOTTOM
                        SNAP_WINDOW_C_RIGHT
                        break;
                    case win::position::right:
                        SNAP_WINDOW_RIGHT
                        SNAP_WINDOW_C_RIGHT
                        break;
                    case win::position::bottom:
                        SNAP_WINDOW_BOTTOM
                        SNAP_WINDOW_C_BOTTOM
                        break;
                    case win::position::top_left:
                        SNAP_WINDOW_TOP
                        SNAP_WINDOW_LEFT
                        SNAP_WINDOW_C_TOP
                        SNAP_WINDOW_C_LEFT
                        break;
                    case win::position::left:
                        SNAP_WINDOW_LEFT
                        SNAP_WINDOW_C_LEFT
                        break;
                    case win::position::top:
                        SNAP_WINDOW_TOP
                        SNAP_WINDOW_C_TOP
                        break;
                    case win::position::top_right:
                        SNAP_WINDOW_TOP
                        SNAP_WINDOW_RIGHT
                        SNAP_WINDOW_C_TOP
                        SNAP_WINDOW_C_RIGHT
                        break;
                    case win::position::bottom_left:
                        SNAP_WINDOW_BOTTOM
                        SNAP_WINDOW_LEFT
                        SNAP_WINDOW_C_BOTTOM
                        SNAP_WINDOW_C_LEFT
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

/**
 * Marks the client as being moved or resized by the user.
 */
void space::setMoveResizeClient(Toplevel* window)
{
    Q_ASSERT(!window || !movingClient); // Catch attempts to move a second
    // window while still moving the first one.
    movingClient = window;
    if (movingClient) {
        ++block_focus;
    } else {
        --block_focus;
    }
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

#ifndef KCMRULES

// ********************
// placement code
// ********************

/**
 * Moves active window left until in bumps into another window or workarea edge.
 */
void space::slotWindowPackLeft()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    win::pack_to(active_client, packPositionLeft(active_client, pos.x(), true), pos.y());
}

void space::slotWindowPackRight()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    auto const width = active_client->geometry_update.frame.size().width();
    win::pack_to(active_client,
                 packPositionRight(active_client, pos.x() + width, true) - width + 1,
                 pos.y());
}

void space::slotWindowPackUp()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    win::pack_to(active_client, pos.x(), packPositionUp(active_client, pos.y(), true));
}

void space::slotWindowPackDown()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    auto const height = active_client->geometry_update.frame.size().height();
    win::pack_to(active_client,
                 pos.x(),
                 packPositionDown(active_client, pos.y() + height, true) - height + 1);
}

void space::slotWindowGrowHorizontal()
{
    if (active_client) {
        win::grow_horizontal(active_client);
    }
}

void space::slotWindowShrinkHorizontal()
{
    if (active_client) {
        win::shrink_horizontal(active_client);
    }
}
void space::slotWindowGrowVertical()
{
    if (active_client) {
        win::grow_vertical(active_client);
    }
}

void space::slotWindowShrinkVertical()
{
    if (active_client) {
        win::shrink_vertical(active_client);
    }
}

void space::quickTileWindow(win::quicktiles mode)
{
    if (!active_client) {
        return;
    }

    // If the user invokes two of these commands in a one second period, try to
    // combine them together to enable easy and intuitive corner tiling
    if (!m_quickTileCombineTimer->isActive()) {
        m_quickTileCombineTimer->start(1000);
        m_lastTilingMode = mode;
    } else {
        auto const was_left_or_right = m_lastTilingMode == win::quicktiles::left
            || m_lastTilingMode == win::quicktiles::right;
        auto const was_top_or_bottom = m_lastTilingMode == win::quicktiles::top
            || m_lastTilingMode == win::quicktiles::bottom;

        auto const is_left_or_right
            = mode == win::quicktiles::left || mode == win::quicktiles::right;
        auto const is_top_or_bottom
            = mode == win::quicktiles::top || mode == win::quicktiles::bottom;

        if ((was_left_or_right && is_top_or_bottom) || (was_top_or_bottom && is_left_or_right)) {
            mode |= m_lastTilingMode;
        }
        m_quickTileCombineTimer->stop();
    }

    win::set_quicktile_mode(active_client, mode, true);
}

int space::packPositionLeft(Toplevel const* window, int oldX, bool leftEdge) const
{
    int newX = clientArea(MaximizeArea, window).left();
    if (oldX <= newX) { // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.left() - 1,
                                 window->geometry_update.frame.center().y()),
                          window->desktop())
                   .left();
    }

    auto const right = newX - win::frame_margins(window).left();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (base::get_intersecting_outputs(kwinApp()->get_base().get_outputs(), frameGeometry).size()
        < 2) {
        newX = right;
    }

    if (oldX <= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : m_windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int x = leftEdge ? win->geometry_update.frame.right() + 1
                               : win->geometry_update.frame.left() - 1;
        if (x > newX && x < oldX
            && !(window->geometry_update.frame.top() > win->geometry_update.frame.bottom()
                 || window->geometry_update.frame.bottom() < win->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

int space::packPositionRight(Toplevel const* window, int oldX, bool rightEdge) const
{
    int newX = clientArea(MaximizeArea, window).right();

    if (oldX >= newX) {
        // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.right() + 1,
                                 window->geometry_update.frame.center().y()),
                          window->desktop())
                   .right();
    }

    auto const right = newX + win::frame_margins(window).right();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (base::get_intersecting_outputs(kwinApp()->get_base().get_outputs(), frameGeometry).size()
        < 2) {
        newX = right;
    }

    if (oldX >= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : m_windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int x = rightEdge ? win->geometry_update.frame.left() - 1
                                : win->geometry_update.frame.right() + 1;
        if (x < newX && x > oldX
            && !(window->geometry_update.frame.top() > win->geometry_update.frame.bottom()
                 || window->geometry_update.frame.bottom() < win->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

int space::packPositionUp(Toplevel const* window, int oldY, bool topEdge) const
{
    int newY = clientArea(MaximizeArea, window).top();
    if (oldY <= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.center().x(),
                                 window->geometry_update.frame.top() - 1),
                          window->desktop())
                   .top();
    }

    if (oldY <= newY) {
        return oldY;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : m_windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int y = topEdge ? win->geometry_update.frame.bottom() + 1
                              : win->geometry_update.frame.top() - 1;
        if (y > newY && y < oldY
            && !(window->geometry_update.frame.left()
                     > win->geometry_update.frame.right() // they overlap in X direction
                 || window->geometry_update.frame.right() < win->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

int space::packPositionDown(Toplevel const* window, int oldY, bool bottomEdge) const
{
    int newY = clientArea(MaximizeArea, window).bottom();
    if (oldY >= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.center().x(),
                                 window->geometry_update.frame.bottom() + 1),
                          window->desktop())
                   .bottom();
    }

    auto const bottom = newY + win::frame_margins(window).bottom();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveBottom(bottom);
    if (base::get_intersecting_outputs(kwinApp()->get_base().get_outputs(), frameGeometry).size()
        < 2) {
        newY = bottom;
    }

    if (oldY >= newY) {
        return oldY;
    }
    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : m_windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int y = bottomEdge ? win->geometry_update.frame.top() - 1
                                 : win->geometry_update.frame.bottom() + 1;
        if (y < newY && y > oldY
            && !(window->geometry_update.frame.left() > win->geometry_update.frame.right()
                 || window->geometry_update.frame.right() < win->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

#endif

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

Toplevel* space::clientUnderMouse(base::output const* output) const
{
    auto it = stacking_order->stack.cend();
    while (it != stacking_order->stack.cbegin()) {
        auto client = *(--it);
        if (!client->control) {
            continue;
        }

        // rule out clients which are not really visible.
        // the screen test is rather superfluous for xrandr & twinview since the geometry would
        // differ -> TODO: might be dropped
        if (!(client->isShown() && client->isOnCurrentDesktop() && win::on_screen(client, output)))
            continue;

        if (client->frameGeometry().contains(input::get_cursor()->pos())) {
            return client;
        }
    }
    return nullptr;
}

void space::gotFocusIn(Toplevel const* window)
{
    if (std::find(should_get_focus.cbegin(), should_get_focus.cend(), const_cast<Toplevel*>(window))
        != should_get_focus.cend()) {
        // remove also all sooner elements that should have got FocusIn,
        // but didn't for some reason (and also won't anymore, because they were sooner)
        while (should_get_focus.front() != window) {
            should_get_focus.pop_front();
        }
        should_get_focus.pop_front(); // remove 'c'
    }
}

void space::setShouldGetFocus(Toplevel* window)
{
    should_get_focus.push_back(window);

    // e.g. fullscreens have different layer when active/not-active
    stacking_order->update_order();
}

namespace FSP
{
enum Level { None = 0, Low, Medium, High, Extreme };
}

// focus_in -> the window got FocusIn event
// ignore_desktop - call comes from _NET_ACTIVE_WINDOW message, don't refuse just because of window
//     is on a different desktop
bool space::allowClientActivation(Toplevel const* window,
                                  xcb_timestamp_t time,
                                  bool focus_in,
                                  bool ignore_desktop)
{
    // kwinApp()->options->focusStealingPreventionLevel :
    // 0 - none    - old KWin behaviour, new windows always get focus
    // 1 - low     - focus stealing prevention is applied normally, when unsure, activation is
    // allowed 2 - normal  - focus stealing prevention is applied normally, when unsure, activation
    // is not allowed,
    //              this is the default
    // 3 - high    - new window gets focus only if it belongs to the active application,
    //              or when no window is currently active
    // 4 - extreme - no window gets focus without user intervention
    if (time == -1U) {
        time = window->userTime();
    }
    auto level
        = window->control->rules().checkFSP(kwinApp()->options->focusStealingPreventionLevel());
    if (session_manager->state() == SessionState::Saving && level <= FSP::Medium) { // <= normal
        return true;
    }
    auto ac = most_recently_activated_window(*this);
    if (focus_in) {
        if (std::find(
                should_get_focus.cbegin(), should_get_focus.cend(), const_cast<Toplevel*>(window))
            != should_get_focus.cend()) {
            // FocusIn was result of KWin's action
            return true;
        }
        // Before getting FocusIn, the active Client already
        // got FocusOut, and therefore got deactivated.
        ac = last_active_client;
    }
    if (time == 0) { // explicitly asked not to get focus
        if (!window->control->rules().checkAcceptFocus(false))
            return false;
    }
    const int protection = ac ? ac->control->rules().checkFPP(2) : 0;

    // stealing is unconditionally allowed (NETWM behavior)
    if (level == FSP::None || protection == FSP::None)
        return true;

    // The active client "grabs" the focus or stealing is generally forbidden
    if (level == FSP::Extreme || protection == FSP::Extreme)
        return false;

    // Desktop switching is only allowed in the "no protection" case
    if (!ignore_desktop && !window->isOnCurrentDesktop())
        return false; // allow only with level == 0

    // No active client, it's ok to pass focus
    // NOTICE that extreme protection needs to be handled before to allow protection on unmanged
    // windows
    if (ac == nullptr || win::is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Activation: No client active, allowing";
        return true; // no active client -> always allow
    }

    // TODO window urgency  -> return true?

    // Unconditionally allow intra-client passing around for lower stealing protections
    // unless the active client has High interest
    if (win::belong_to_same_client(window, ac, win::same_client_check::relaxed_for_active)
        && protection < FSP::High) {
        qCDebug(KWIN_CORE) << "Activation: Belongs to active application";
        return true;
    }

    if (!window->isOnCurrentDesktop()) {
        // we allowed explicit self-activation across virtual desktops
        // inside a client or if no client was active, but not otherwise
        return false;
    }

    // High FPS, not intr-client change. Only allow if the active client has only minor interest
    if (level > FSP::Medium && protection > FSP::Low)
        return false;

    if (time == -1U) { // no time known
        qCDebug(KWIN_CORE) << "Activation: No timestamp at all";
        // Only allow for Low protection unless active client has High interest in focus
        if (level < FSP::Medium && protection < FSP::High)
            return true;
        // no timestamp at all, don't activate - because there's also creation timestamp
        // done on CreateNotify, this case should happen only in case application
        // maps again already used window, i.e. this won't happen after app startup
        return false;
    }

    // Low or medium FSP, usertime comparism is possible
    const xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Activation, compared:" << window << ":" << time << ":" << user_time
                       << ":" << (NET::timestampCompare(time, user_time) >= 0);
    return NET::timestampCompare(time, user_time) >= 0; // time >= user_time
}

// basically the same like allowClientActivation(), this time allowing
// a window to be fully raised upon its own request (XRaiseWindow),
// if refused, it will be raised only on top of windows belonging
// to the same application
bool space::allowFullClientRaising(Toplevel const* window, xcb_timestamp_t time)
{
    auto level
        = window->control->rules().checkFSP(kwinApp()->options->focusStealingPreventionLevel());
    if (session_manager->state() == SessionState::Saving && level <= 2) { // <= normal
        return true;
    }
    auto ac = most_recently_activated_window(*this);
    if (level == 0) // none
        return true;
    if (level == 4) // extreme
        return false;
    if (ac == nullptr || win::is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Raising: No client active, allowing";
        return true; // no active client -> always allow
    }
    // TODO window urgency  -> return true?
    if (win::belong_to_same_client(window, ac, win::same_client_check::relaxed_for_active)) {
        qCDebug(KWIN_CORE) << "Raising: Belongs to active application";
        return true;
    }
    if (level == 3) // high
        return false;
    xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Raising, compared:" << time << ":" << user_time << ":"
                       << (NET::timestampCompare(time, user_time) >= 0);
    return NET::timestampCompare(time, user_time) >= 0; // time >= user_time
}

// called from Client after FocusIn that wasn't initiated by KWin and the client
// wasn't allowed to activate
void space::restoreFocus()
{
    // this updateXTime() is necessary - as FocusIn events don't have
    // a timestamp *sigh*, kwin's timestamp would be older than the timestamp
    // that was used by whoever caused the focus change, and therefore
    // the attempt to restore the focus would fail due to old timestamp
    kwinApp()->update_x11_time_from_clock();
    if (should_get_focus.size() > 0) {
        request_focus(*this, should_get_focus.back());
    } else if (last_active_client) {
        request_focus(*this, last_active_client);
    }
}

void space::clientAttentionChanged(Toplevel* window, bool set)
{
    remove_all(attention_chain, window);
    if (set) {
        attention_chain.push_front(window);
    }
    Q_EMIT qobject->clientDemandsAttentionChanged(window, set);
}

/// X11 event handling

#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
typedef struct xcb_ge_generic_event_t {
    uint8_t response_type;  /**<  */
    uint8_t extension;      /**<  */
    uint16_t sequence;      /**<  */
    uint32_t length;        /**<  */
    uint16_t event_type;    /**<  */
    uint8_t pad0[22];       /**<  */
    uint32_t full_sequence; /**<  */
} xcb_ge_generic_event_t;
#endif

// Used only to filter events that need to be processed by Qt first
// (e.g. keyboard input to be composed), otherwise events are
// handle by the XEvent filter above
bool space::workspaceEvent(QEvent* e)
{
    if ((e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease
         || e->type() == QEvent::ShortcutOverride)
        && render.effects && render.effects->hasKeyboardGrab()) {
        render.effects->grabbedKeyboardEvent(static_cast<QKeyEvent*>(e));
        return true;
    }
    return false;
}

void space::slotIncreaseWindowOpacity()
{
    if (!active_client) {
        return;
    }
    active_client->setOpacity(qMin(active_client->opacity() + 0.05, 1.0));
}

void space::slotLowerWindowOpacity()
{
    if (!active_client) {
        return;
    }
    active_client->setOpacity(qMax(active_client->opacity() - 0.05, 0.05));
}

QAction* prepare_shortcut_action(win::space& space,
                                 QString const& actionName,
                                 QString const& description,
                                 QKeySequence const& shortcut,
                                 QVariant const& data)
{
    auto action = new QAction(space.qobject.get());
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(actionName);
    action->setText(description);
    if (data.isValid()) {
        action->setData(data);
    }
    KGlobalAccel::self()->setDefaultShortcut(action, QList<QKeySequence>() << shortcut);
    KGlobalAccel::self()->setShortcut(action, QList<QKeySequence>() << shortcut);
    return action;
}

template<typename Slot>
void space::initShortcut(const QString& actionName,
                         const QString& description,
                         const QKeySequence& shortcut,
                         Slot slot,
                         const QVariant& data)
{
    initShortcut(actionName, description, shortcut, qobject.get(), slot, data);
}

template<typename T, typename Slot>
void space::initShortcut(const QString& actionName,
                         const QString& description,
                         const QKeySequence& shortcut,
                         T* receiver,
                         Slot slot,
                         const QVariant& data)
{
    auto action = prepare_shortcut_action(*this, actionName, description, shortcut, data);
    kwinApp()->input->registerShortcut(shortcut, action, receiver, slot);
}

template<typename T, typename Slot>
void space::init_shortcut_with_action_arg(const QString& actionName,
                                          const QString& description,
                                          const QKeySequence& shortcut,
                                          T* receiver,
                                          Slot slot,
                                          QVariant const& data)
{
    auto action = prepare_shortcut_action(*this, actionName, description, shortcut, data);
    kwinApp()->input->registerShortcut(
        shortcut, action, receiver, [this, action, &slot] { slot(action); });
}

/**
 * Creates the global accel object \c keys.
 */
void space::initShortcuts()
{
    // Some shortcuts have Tarzan-speech like names, they need extra
    // normal human descriptions with DEF2() the others can use DEF()
    // new DEF3 allows to pass data to the action, replacing the %1 argument in the name

#define DEF2(name, descr, key, fnSlot)                                                             \
    initShortcut(QStringLiteral(name), descr.toString(), key, [this] { fnSlot(); });

#define DEF(name, key, fnSlot)                                                                     \
    initShortcut(                                                                                  \
        QString::fromUtf8(name.untranslatedText()), name.toString(), key, [this] { fnSlot(); });

#define DEF3(name, key, fnSlot, value)                                                             \
    init_shortcut_with_action_arg(                                                                 \
        QString::fromUtf8(name.untranslatedText()).arg(value),                                     \
        name.subs(value).toString(),                                                               \
        key,                                                                                       \
        qobject.get(),                                                                             \
        [this](QAction* action) { fnSlot(action); },                                               \
        value);

#define DEF4(name, descr, key, functor)                                                            \
    initShortcut(QStringLiteral(name), descr.toString(), key, functor);

#define DEF5(name, key, functor, value)                                                            \
    initShortcut(QString::fromUtf8(name.untranslatedText()).arg(value),                            \
                 name.subs(value).toString(),                                                      \
                 key,                                                                              \
                 functor,                                                                          \
                 value);

#define DEF6(name, key, target, fnSlot)                                                            \
    initShortcut(QString::fromUtf8(name.untranslatedText()), name.toString(), key, target, &fnSlot);

    DEF(kli18n("Window Operations Menu"), Qt::ALT + Qt::Key_F3, slotWindowOperations);
    DEF2("Window Close", kli18n("Close Window"), Qt::ALT + Qt::Key_F4, slotWindowClose);
    DEF2("Window Maximize",
         kli18n("Maximize Window"),
         Qt::META + Qt::Key_PageUp,
         slotWindowMaximize);
    DEF2("Window Maximize Vertical",
         kli18n("Maximize Window Vertically"),
         0,
         slotWindowMaximizeVertical);
    DEF2("Window Maximize Horizontal",
         kli18n("Maximize Window Horizontally"),
         0,
         slotWindowMaximizeHorizontal);
    DEF2("Window Minimize",
         kli18n("Minimize Window"),
         Qt::META + Qt::Key_PageDown,
         slotWindowMinimize);
    DEF2("Window Move", kli18n("Move Window"), 0, slotWindowMove);
    DEF2("Window Resize", kli18n("Resize Window"), 0, slotWindowResize);
    DEF2("Window Raise", kli18n("Raise Window"), 0, slotWindowRaise);
    DEF2("Window Lower", kli18n("Lower Window"), 0, slotWindowLower);
    DEF(kli18n("Toggle Window Raise/Lower"), 0, slotWindowRaiseOrLower);
    DEF2("Window Fullscreen", kli18n("Make Window Fullscreen"), 0, slotWindowFullScreen);
    DEF2("Window No Border", kli18n("Hide Window Border"), 0, slotWindowNoBorder);
    DEF2("Window Above Other Windows", kli18n("Keep Window Above Others"), 0, slotWindowAbove);
    DEF2("Window Below Other Windows", kli18n("Keep Window Below Others"), 0, slotWindowBelow);
    DEF(kli18n("Activate Window Demanding Attention"),
        Qt::META | Qt::CTRL | Qt::Key_A,
        slotActivateAttentionWindow);
    DEF(kli18n("Setup Window Shortcut"), 0, slotSetupWindowShortcut);
    DEF2("Window Pack Right", kli18n("Pack Window to the Right"), 0, slotWindowPackRight);
    DEF2("Window Pack Left", kli18n("Pack Window to the Left"), 0, slotWindowPackLeft);
    DEF2("Window Pack Up", kli18n("Pack Window Up"), 0, slotWindowPackUp);
    DEF2("Window Pack Down", kli18n("Pack Window Down"), 0, slotWindowPackDown);
    DEF2("Window Grow Horizontal",
         kli18n("Pack Grow Window Horizontally"),
         0,
         slotWindowGrowHorizontal);
    DEF2("Window Grow Vertical", kli18n("Pack Grow Window Vertically"), 0, slotWindowGrowVertical);
    DEF2("Window Shrink Horizontal",
         kli18n("Pack Shrink Window Horizontally"),
         0,
         slotWindowShrinkHorizontal);
    DEF2("Window Shrink Vertical",
         kli18n("Pack Shrink Window Vertically"),
         0,
         slotWindowShrinkVertical);
    DEF4("Window Quick Tile Left",
         kli18n("Quick Tile Window to the Left"),
         Qt::META + Qt::Key_Left,
         std::bind(&space::quickTileWindow, this, win::quicktiles::left));
    DEF4("Window Quick Tile Right",
         kli18n("Quick Tile Window to the Right"),
         Qt::META + Qt::Key_Right,
         std::bind(&space::quickTileWindow, this, win::quicktiles::right));
    DEF4("Window Quick Tile Top",
         kli18n("Quick Tile Window to the Top"),
         Qt::META + Qt::Key_Up,
         std::bind(&space::quickTileWindow, this, win::quicktiles::top));
    DEF4("Window Quick Tile Bottom",
         kli18n("Quick Tile Window to the Bottom"),
         Qt::META + Qt::Key_Down,
         std::bind(&space::quickTileWindow, this, win::quicktiles::bottom));
    DEF4("Window Quick Tile Top Left",
         kli18n("Quick Tile Window to the Top Left"),
         0,
         std::bind(&space::quickTileWindow, this, win::quicktiles::top | win::quicktiles::left));
    DEF4("Window Quick Tile Bottom Left",
         kli18n("Quick Tile Window to the Bottom Left"),
         0,
         std::bind(&space::quickTileWindow, this, win::quicktiles::bottom | win::quicktiles::left));
    DEF4("Window Quick Tile Top Right",
         kli18n("Quick Tile Window to the Top Right"),
         0,
         std::bind(&space::quickTileWindow, this, win::quicktiles::top | win::quicktiles::right));
    DEF4(
        "Window Quick Tile Bottom Right",
        kli18n("Quick Tile Window to the Bottom Right"),
        0,
        std::bind(&space::quickTileWindow, this, win::quicktiles::bottom | win::quicktiles::right));
    DEF4("Switch Window Up",
         kli18n("Switch to Window Above"),
         Qt::META + Qt::ALT + Qt::Key_Up,
         std::bind(
             static_cast<void (space::*)(Direction)>(&space::switchWindow), this, DirectionNorth));
    DEF4("Switch Window Down",
         kli18n("Switch to Window Below"),
         Qt::META + Qt::ALT + Qt::Key_Down,
         std::bind(
             static_cast<void (space::*)(Direction)>(&space::switchWindow), this, DirectionSouth));
    DEF4("Switch Window Right",
         kli18n("Switch to Window to the Right"),
         Qt::META + Qt::ALT + Qt::Key_Right,
         std::bind(
             static_cast<void (space::*)(Direction)>(&space::switchWindow), this, DirectionEast));
    DEF4("Switch Window Left",
         kli18n("Switch to Window to the Left"),
         Qt::META + Qt::ALT + Qt::Key_Left,
         std::bind(
             static_cast<void (space::*)(Direction)>(&space::switchWindow), this, DirectionWest));
    DEF2("Increase Opacity",
         kli18n("Increase Opacity of Active Window by 5 %"),
         0,
         slotIncreaseWindowOpacity);
    DEF2("Decrease Opacity",
         kli18n("Decrease Opacity of Active Window by 5 %"),
         0,
         slotLowerWindowOpacity);

    DEF2("Window On All Desktops",
         kli18n("Keep Window on All Desktops"),
         0,
         slotWindowOnAllDesktops);

    for (int i = 1; i < 21; ++i) {
        DEF5(kli18n("Window to Desktop %1"), 0, std::bind(&space::slotWindowToDesktop, this, i), i);
    }
    DEF(kli18n("Window to Next Desktop"), 0, slotWindowToNextDesktop);
    DEF(kli18n("Window to Previous Desktop"), 0, slotWindowToPreviousDesktop);
    DEF(kli18n("Window One Desktop to the Right"), 0, slotWindowToDesktopRight);
    DEF(kli18n("Window One Desktop to the Left"), 0, slotWindowToDesktopLeft);
    DEF(kli18n("Window One Desktop Up"), 0, slotWindowToDesktopUp);
    DEF(kli18n("Window One Desktop Down"), 0, slotWindowToDesktopDown);

    for (int i = 0; i < 8; ++i) {
        DEF3(
            kli18n("Window to Screen %1"),
            0,
            [this](QAction* action) { slotWindowToScreen(action); },
            i);
    }
    DEF(kli18n("Window to Next Screen"), 0, slotWindowToNextScreen);
    DEF(kli18n("Window to Previous Screen"), 0, slotWindowToPrevScreen);
    DEF(kli18n("Show Desktop"), Qt::META + Qt::Key_D, slotToggleShowDesktop);

    for (int i = 0; i < 8; ++i) {
        DEF3(
            kli18n("Switch to Screen %1"),
            0,
            [this](QAction* action) { slotSwitchToScreen(action); },
            i);
    }

    DEF(kli18n("Switch to Next Screen"), 0, slotSwitchToNextScreen);
    DEF(kli18n("Switch to Previous Screen"), 0, slotSwitchToPrevScreen);

    DEF(kli18n("Kill Window"), Qt::META | Qt::CTRL | Qt::Key_Escape, slotKillWindow);
    DEF6(kli18n("Suspend Compositing"),
         Qt::SHIFT + Qt::ALT + Qt::Key_F12,
         &render,
         render::compositor::toggleCompositing);
    DEF6(kli18n("Invert Screen Colors"),
         0,
         kwinApp()->get_base().render.get(),
         render::platform::invertScreen);

#undef DEF
#undef DEF2
#undef DEF3
#undef DEF4
#undef DEF5
#undef DEF6

#if KWIN_BUILD_TABBOX
    tabbox->init_shortcuts();
#endif
    virtual_desktop_manager->initShortcuts();
    kwinApp()->get_base().render->night_color->init_shortcuts();

    // so that it's recreated next time
    user_actions_menu->discard();
}

void space::setupWindowShortcut(Toplevel* window)
{
    Q_ASSERT(client_keys_dialog == nullptr);
    // TODO: PORT ME (KGlobalAccel related)
    // keys->setEnabled( false );
    // disable_shortcuts_keys->setEnabled( false );
    // client_keys->setEnabled( false );
    client_keys_dialog = new win::shortcut_dialog(window->control->shortcut());
    client_keys_client = window;

    QObject::connect(client_keys_dialog,
                     &win::shortcut_dialog::dialogDone,
                     qobject.get(),
                     [this](auto&& ok) { setupWindowShortcutDone(ok); });

    auto area = clientArea(ScreenArea, window);
    auto size = client_keys_dialog->sizeHint();

    auto pos = win::frame_to_client_pos(window, window->pos());
    if (pos.x() + size.width() >= area.right()) {
        pos.setX(area.right() - size.width());
    }
    if (pos.y() + size.height() >= area.bottom()) {
        pos.setY(area.bottom() - size.height());
    }

    client_keys_dialog->move(pos);
    client_keys_dialog->show();
    active_popup = client_keys_dialog;
    active_popup_client = window;
}

void space::setupWindowShortcutDone(bool ok)
{
    //    keys->setEnabled( true );
    //    disable_shortcuts_keys->setEnabled( true );
    //    client_keys->setEnabled( true );
    if (ok)
        win::set_shortcut(client_keys_client, client_keys_dialog->shortcut().toString());
    close_active_popup(*this);
    client_keys_dialog->deleteLater();
    client_keys_dialog = nullptr;
    client_keys_client = nullptr;
    if (active_client)
        active_client->takeFocus();
}

void space::clientShortcutUpdated(Toplevel* window)
{
    QString key = QStringLiteral("_k_session:%1").arg(window->xcb_window);
    auto action = qobject->findChild<QAction*>(key);
    if (!window->control->shortcut().isEmpty()) {
        if (action == nullptr) { // new shortcut
            action = new QAction(qobject.get());
            kwinApp()->input->setup_action_for_global_accel(action);
            action->setProperty("componentName", QStringLiteral(KWIN_NAME));
            action->setObjectName(key);
            action->setText(i18n("Activate Window (%1)", win::caption(window)));
            QObject::connect(action, &QAction::triggered, window, [this, window] {
                force_activate_window(*this, window);
            });
        }

        // no autoloading, since it's configured explicitly here and is not meant to be reused
        // (the key is the window id anyway, which is kind of random)
        KGlobalAccel::self()->setShortcut(action,
                                          QList<QKeySequence>() << window->control->shortcut(),
                                          KGlobalAccel::NoAutoloading);
        action->setEnabled(true);
    } else {
        KGlobalAccel::self()->removeAllShortcuts(action);
        delete action;
    }
}

void space::performWindowOperation(Toplevel* window, base::options::WindowOperation op)
{
    if (!window) {
        return;
    }

    auto cursor = input::get_cursor();

    if (op == base::options::MoveOp || op == base::options::UnrestrictedMoveOp) {
        cursor->set_pos(window->frameGeometry().center());
    }
    if (op == base::options::ResizeOp || op == base::options::UnrestrictedResizeOp) {
        cursor->set_pos(window->frameGeometry().bottomRight());
    }

    switch (op) {
    case base::options::MoveOp:
        window->performMouseCommand(base::options::MouseMove, cursor->pos());
        break;
    case base::options::UnrestrictedMoveOp:
        window->performMouseCommand(base::options::MouseUnrestrictedMove, cursor->pos());
        break;
    case base::options::ResizeOp:
        window->performMouseCommand(base::options::MouseResize, cursor->pos());
        break;
    case base::options::UnrestrictedResizeOp:
        window->performMouseCommand(base::options::MouseUnrestrictedResize, cursor->pos());
        break;
    case base::options::CloseOp:
        QMetaObject::invokeMethod(window, "closeWindow", Qt::QueuedConnection);
        break;
    case base::options::MaximizeOp:
        win::maximize(window,
                      window->maximizeMode() == win::maximize_mode::full
                          ? win::maximize_mode::restore
                          : win::maximize_mode::full);
        break;
    case base::options::HMaximizeOp:
        win::maximize(window, window->maximizeMode() ^ win::maximize_mode::horizontal);
        break;
    case base::options::VMaximizeOp:
        win::maximize(window, window->maximizeMode() ^ win::maximize_mode::vertical);
        break;
    case base::options::RestoreOp:
        win::maximize(window, win::maximize_mode::restore);
        break;
    case base::options::MinimizeOp:
        win::set_minimized(window, true);
        break;
    case base::options::OnAllDesktopsOp:
        win::set_on_all_desktops(window, !window->isOnAllDesktops());
        break;
    case base::options::FullScreenOp:
        window->setFullScreen(!window->control->fullscreen(), true);
        break;
    case base::options::NoBorderOp:
        window->setNoBorder(!window->noBorder());
        break;
    case base::options::KeepAboveOp: {
        blocker block(stacking_order);
        bool was = window->control->keep_above();
        win::set_keep_above(window, !window->control->keep_above());
        if (was && !window->control->keep_above()) {
            win::raise_window(this, window);
        }
        break;
    }
    case base::options::KeepBelowOp: {
        blocker block(stacking_order);
        bool was = window->control->keep_below();
        win::set_keep_below(window, !window->control->keep_below());
        if (was && !window->control->keep_below()) {
            win::lower_window(this, window);
        }
        break;
    }
    case base::options::WindowRulesOp:
        rule_book->edit(window, false);
        break;
    case base::options::ApplicationRulesOp:
        rule_book->edit(window, true);
        break;
    case base::options::SetupWindowShortcutOp:
        setupWindowShortcut(window);
        break;
    case base::options::LowerOp:
        win::lower_window(this, window);
        break;
    case base::options::OperationsOp:
    case base::options::NoOp:
        break;
    }
}

void space::slotActivateAttentionWindow()
{
    if (attention_chain.size() > 0) {
        activate_window(*this, attention_chain.front());
    }
}

static uint senderValue(QAction* act)
{
    bool ok = false;
    uint i = -1;
    if (act)
        i = act->data().toUInt(&ok);
    if (ok)
        return i;
    return -1;
}

#define USABLE_ACTIVE_CLIENT                                                                       \
    (active_client && !(win::is_desktop(active_client) || win::is_dock(active_client)))

void space::slotWindowToDesktop(uint i)
{
    if (USABLE_ACTIVE_CLIENT) {
        if (i < 1)
            return;

        if (i >= 1 && i <= virtual_desktop_manager->count())
            send_window_to_desktop(*this, active_client, i, true);
    }
}

static bool screenSwitchImpossible()
{
    if (!kwinApp()->options->get_current_output_follows_mouse()) {
        return false;
    }
    QStringList args;
    args << QStringLiteral("--passivepopup")
         << i18n(
                "The window manager is configured to consider the screen with the mouse on it as "
                "active one.\n"
                "Therefore it is not possible to switch to a screen explicitly.")
         << QStringLiteral("20");
    KProcess::startDetached(QStringLiteral("kdialog"), args);
    return true;
}

void space::slotSwitchToScreen(QAction* action)
{
    if (screenSwitchImpossible()) {
        return;
    }

    int const screen = senderValue(action);
    auto output = base::get_output(kwinApp()->get_base().get_outputs(), screen);

    if (output) {
        set_current_output(*this, *output);
    }
}

base::output const* get_derivated_output(base::output const* output, int drift)
{
    auto const& outputs = kwinApp()->get_base().get_outputs();
    auto index = output ? base::get_output_index(outputs, *output) : 0;
    index += drift;
    return base::get_output(outputs, index % outputs.size());
}

base::output const* get_derivated_output(win::space& space, int drift)
{
    return get_derivated_output(get_current_output(space), drift);
}

void space::slotSwitchToNextScreen()
{
    if (screenSwitchImpossible()) {
        return;
    }
    if (auto output = get_derivated_output(*this, 1)) {
        set_current_output(*this, *output);
    }
}

void space::slotSwitchToPrevScreen()
{
    if (screenSwitchImpossible()) {
        return;
    }
    if (auto output = get_derivated_output(*this, -1)) {
        set_current_output(*this, *output);
    }
}

void space::slotWindowToScreen(QAction* action)
{
    if (USABLE_ACTIVE_CLIENT) {
        int const screen = senderValue(action);
        auto output = base::get_output(kwinApp()->get_base().get_outputs(), screen);
        if (output) {
            send_to_screen(*this, active_client, *output);
        }
    }
}

void space::slotWindowToNextScreen()
{
    if (!USABLE_ACTIVE_CLIENT) {
        return;
    }
    if (auto output = get_derivated_output(active_client->central_output, 1)) {
        send_to_screen(*this, active_client, *output);
    }
}

void space::slotWindowToPrevScreen()
{
    if (!USABLE_ACTIVE_CLIENT) {
        return;
    }
    if (auto output = get_derivated_output(active_client->central_output, -1)) {
        send_to_screen(*this, active_client, *output);
    }
}

/**
 * Maximizes the active client.
 */
void space::slotWindowMaximize()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::MaximizeOp);
}

/**
 * Maximizes the active client vertically.
 */
void space::slotWindowMaximizeVertical()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::VMaximizeOp);
}

/**
 * Maximizes the active client horiozontally.
 */
void space::slotWindowMaximizeHorizontal()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::HMaximizeOp);
}

/**
 * Minimizes the active client.
 */
void space::slotWindowMinimize()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::MinimizeOp);
}

/**
 * Raises the active client.
 */
void space::slotWindowRaise()
{
    if (USABLE_ACTIVE_CLIENT) {
        win::raise_window(this, active_client);
    }
}

/**
 * Lowers the active client.
 */
void space::slotWindowLower()
{
    if (USABLE_ACTIVE_CLIENT) {
        win::lower_window(this, active_client);
        // As this most likely makes the window no longer visible change the
        // keyboard focus to the next available window.
        // activateNextClient( c ); // Doesn't work when we lower a child window
        if (active_client->control->active() && kwinApp()->options->focusPolicyIsReasonable()) {
            if (kwinApp()->options->isNextFocusPrefersMouse()) {
                auto next = clientUnderMouse(active_client->central_output);
                if (next && next != active_client)
                    request_focus(*this, next);
            } else {
                activate_window(
                    *this,
                    top_client_on_desktop(this, virtual_desktop_manager->current(), nullptr));
            }
        }
    }
}

/**
 * Does a toggle-raise-and-lower on the active client.
 */
void space::slotWindowRaiseOrLower()
{
    if (USABLE_ACTIVE_CLIENT)
        win::raise_or_lower_client(this, active_client);
}

void space::slotWindowOnAllDesktops()
{
    if (USABLE_ACTIVE_CLIENT)
        win::set_on_all_desktops(active_client, !active_client->isOnAllDesktops());
}

void space::slotWindowFullScreen()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::FullScreenOp);
}

void space::slotWindowNoBorder()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::NoBorderOp);
}

void space::slotWindowAbove()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::KeepAboveOp);
}

void space::slotWindowBelow()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::KeepBelowOp);
}
void space::slotSetupWindowShortcut()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::SetupWindowShortcutOp);
}

/**
 * Toggles show desktop.
 */
void space::slotToggleShowDesktop()
{
    setShowingDesktop(!showingDesktop());
}

template<typename Direction>
void windowToDesktop(Toplevel& window)
{
    auto& ws = window.space;
    auto& vds = ws.virtual_desktop_manager;
    Direction functor(*vds);
    // TODO: why is kwinApp()->options->isRollOverDesktops() not honored?
    const auto desktop = functor(nullptr, true);
    if (!win::is_desktop(&window) && !win::is_dock(&window)) {
        ws.setMoveResizeClient(&window);
        vds->setCurrent(desktop);
        ws.setMoveResizeClient(nullptr);
    }
}

/**
 * Moves the active client to the next desktop.
 */
void space::slotWindowToNextDesktop()
{
    if (USABLE_ACTIVE_CLIENT)
        windowToNextDesktop(*active_client);
}

void space::windowToNextDesktop(Toplevel& window)
{
    windowToDesktop<win::virtual_desktop_next>(window);
}

/**
 * Moves the active client to the previous desktop.
 */
void space::slotWindowToPreviousDesktop()
{
    if (USABLE_ACTIVE_CLIENT)
        windowToPreviousDesktop(*active_client);
}

void space::windowToPreviousDesktop(Toplevel& window)
{
    windowToDesktop<win::virtual_desktop_previous>(window);
}

template<typename Direction>
void activeClientToDesktop(win::space& space)
{
    auto& vds = space.virtual_desktop_manager;
    const int current = vds->current();
    Direction functor(*vds);
    const int d = functor(current, kwinApp()->options->isRollOverDesktops());
    if (d == current) {
        return;
    }
    space.setMoveResizeClient(space.active_client);
    vds->setCurrent(d);
    space.setMoveResizeClient(nullptr);
}

void space::slotWindowToDesktopRight()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_right>(*this);
    }
}

void space::slotWindowToDesktopLeft()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_left>(*this);
    }
}

void space::slotWindowToDesktopUp()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_above>(*this);
    }
}

void space::slotWindowToDesktopDown()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_below>(*this);
    }
}

/**
 * Kill Window feature, similar to xkill.
 */
void space::slotKillWindow()
{
    if (!window_killer) {
        window_killer = std::make_unique<kill_window<space>>(*this);
    }
    window_killer->start();
}

/**
 * Switches to the nearest window in given direction.
 */
void space::switchWindow(Direction direction)
{
    if (!active_client)
        return;
    auto c = active_client;
    int desktopNumber = c->isOnAllDesktops() ? virtual_desktop_manager->current() : c->desktop();

    // Centre of the active window
    QPoint curPos(c->pos().x() + c->size().width() / 2, c->pos().y() + c->size().height() / 2);

    if (!switchWindow(c, direction, curPos, desktopNumber)) {
        auto opposite = [&] {
            switch (direction) {
            case DirectionNorth:
                return QPoint(curPos.x(), kwinApp()->get_base().topology.size.height());
            case DirectionSouth:
                return QPoint(curPos.x(), 0);
            case DirectionEast:
                return QPoint(0, curPos.y());
            case DirectionWest:
                return QPoint(kwinApp()->get_base().topology.size.width(), curPos.y());
            default:
                Q_UNREACHABLE();
            }
        };

        switchWindow(c, direction, opposite(), desktopNumber);
    }
}

bool space::switchWindow(Toplevel* c, Direction direction, QPoint curPos, int d)
{
    Toplevel* switchTo = nullptr;
    int bestScore = 0;

    auto clist = stacking_order->stack;
    for (auto i = clist.rbegin(); i != clist.rend(); ++i) {
        auto client = *i;
        if (!client->control) {
            continue;
        }
        if (win::wants_tab_focus(client) && *i != c && client->isOnDesktop(d)
            && !client->control->minimized()) {
            // Centre of the other window
            const QPoint other(client->pos().x() + client->size().width() / 2,
                               client->pos().y() + client->size().height() / 2);

            int distance;
            int offset;
            switch (direction) {
            case DirectionNorth:
                distance = curPos.y() - other.y();
                offset = qAbs(other.x() - curPos.x());
                break;
            case DirectionEast:
                distance = other.x() - curPos.x();
                offset = qAbs(other.y() - curPos.y());
                break;
            case DirectionSouth:
                distance = other.y() - curPos.y();
                offset = qAbs(other.x() - curPos.x());
                break;
            case DirectionWest:
                distance = curPos.x() - other.x();
                offset = qAbs(other.y() - curPos.y());
                break;
            default:
                distance = -1;
                offset = -1;
            }

            if (distance > 0) {
                // Inverse score
                int score = distance + offset + ((offset * offset) / distance);
                if (score < bestScore || !switchTo) {
                    switchTo = client;
                    bestScore = score;
                }
            }
        }
    }
    if (switchTo) {
        activate_window(*this, switchTo);
    }

    return switchTo;
}

/**
 * Shows the window operations popup menu for the active client.
 */
void space::slotWindowOperations()
{
    if (!active_client)
        return;
    auto pos = win::frame_to_client_pos(active_client, active_client->pos());
    showWindowMenu(QRect(pos, pos), active_client);
}

void space::showWindowMenu(const QRect& pos, Toplevel* window)
{
    user_actions_menu->show(pos, window);
}

void space::showApplicationMenu(const QRect& pos, Toplevel* window, int actionId)
{
    appmenu->showApplicationMenu(window->pos() + pos.bottomLeft(), window, actionId);
}

/**
 * Closes the active client.
 */
void space::slotWindowClose()
{
    // TODO: why?
    //     if ( tab_box->isVisible())
    //         return;
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::CloseOp);
}

/**
 * Starts keyboard move mode for the active client.
 */
void space::slotWindowMove()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::UnrestrictedMoveOp);
}

/**
 * Starts keyboard resize mode for the active client.
 */
void space::slotWindowResize()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, base::options::UnrestrictedResizeOp);
}

#undef USABLE_ACTIVE_CLIENT

bool space::shortcutAvailable(const QKeySequence& cut, Toplevel* ignore) const
{
    if (ignore && cut == ignore->control->shortcut())
        return true;

    // Check if the shortcut is already registered.
    auto const registeredShortcuts = KGlobalAccel::globalShortcutsByKey(cut);
    for (auto const& shortcut : registeredShortcuts) {
        // Only return "not available" if is not a client activation shortcut, as it may be no
        // longer valid.
        if (!shortcut.uniqueName().startsWith(QStringLiteral("_k_session:"))) {
            return false;
        }
    }

    // Check now conflicts with activation shortcuts for current clients.
    for (auto const win : m_windows) {
        if (win != ignore && win->control && win->control->shortcut() == cut) {
            return false;
        }
    }

    return true;
}

static KConfig* sessionConfig(QString id, QString key)
{
    static KConfig* config = nullptr;
    static QString lastId;
    static QString lastKey;
    static QString pattern
        = QString(QLatin1String("session/%1_%2_%3")).arg(qApp->applicationName());

    if (id != lastId || key != lastKey) {
        delete config;
        config = nullptr;
    }

    lastId = id;
    lastKey = key;

    if (!config) {
        config = new KConfig(pattern.arg(id).arg(key), KConfig::SimpleConfig);
    }

    return config;
}

static const char* const window_type_names[] = {"Unknown",
                                                "Normal",
                                                "Desktop",
                                                "Dock",
                                                "Toolbar",
                                                "Menu",
                                                "Dialog",
                                                "Override",
                                                "TopMenu",
                                                "Utility",
                                                "Splash"};
// change also the two functions below when adding new entries

static const char* windowTypeToTxt(NET::WindowType type)
{
    if (type >= NET::Unknown && type <= NET::Splash) {
        // +1 (unknown==-1)
        return window_type_names[type + 1];
    }

    if (type == -2) {
        // undefined (not really part of NET::WindowType)
        return "Undefined";
    }

    qFatal("Unknown Window Type");
    return nullptr;
}

static NET::WindowType txtToWindowType(const char* txt)
{
    for (int i = NET::Unknown; i <= NET::Splash; ++i) {
        // Compare with window_type_names at i+1.
        if (qstrcmp(txt, window_type_names[i + 1]) == 0) {
            return static_cast<NET::WindowType>(i);
        }
    }

    // undefined
    return static_cast<NET::WindowType>(-2);
}

/**
 * Stores the current session in the config file
 *
 * @see loadSessionInfo
 */
void space::storeSession(const QString& sessionName, win::sm_save_phase phase)
{
    qCDebug(KWIN_CORE) << "storing session" << sessionName << "in phase" << phase;
    KConfig* config = sessionConfig(sessionName, QString());

    KConfigGroup cg(config, "Session");
    int count = 0;
    int active_client = -1;

    for (auto const& window : m_windows) {
        if (!window->control) {
            continue;
        }
        auto x11_client = qobject_cast<x11::window*>(window);
        if (!x11_client) {
            continue;
        }

        if (x11_client->windowType() > NET::Splash) {
            // window types outside this are not tooltips/menus/OSDs
            // typically these will be unmanaged and not in this list anyway, but that is not
            // enforced
            continue;
        }

        QByteArray sessionId = x11_client->sessionId();
        QByteArray wmCommand = x11_client->wmCommand();

        if (sessionId.isEmpty()) {
            // remember also applications that are not XSMP capable
            // and use the obsolete WM_COMMAND / WM_SAVE_YOURSELF
            if (wmCommand.isEmpty()) {
                continue;
            }
        }

        count++;
        if (x11_client->control->active()) {
            active_client = count;
        }

        if (phase == win::sm_save_phase2 || phase == win::sm_save_phase2_full) {
            storeClient(cg, count, x11_client);
        }
    }

    if (phase == win::sm_save_phase0) {
        // it would be much simpler to save these values to the config file,
        // but both Qt and KDE treat phase1 and phase2 separately,
        // which results in different sessionkey and different config file :(
        session_active_client = active_client;
        session_desktop = virtual_desktop_manager->current();
    } else if (phase == win::sm_save_phase2) {
        cg.writeEntry("count", count);
        cg.writeEntry("active", session_active_client);
        cg.writeEntry("desktop", session_desktop);
    } else {
        // SMSavePhase2Full
        cg.writeEntry("count", count);
        cg.writeEntry("active", session_active_client);
        cg.writeEntry("desktop", virtual_desktop_manager->current());
    }

    // it previously did some "revert to defaults" stuff for phase1 I think
    config->sync();
}

void space::storeClient(KConfigGroup& cg, int num, win::x11::window* c)
{
    QString n = QString::number(num);
    cg.writeEntry(QLatin1String("sessionId") + n, c->sessionId().constData());
    cg.writeEntry(QLatin1String("windowRole") + n, c->windowRole().constData());
    cg.writeEntry(QLatin1String("wmCommand") + n, c->wmCommand().constData());
    cg.writeEntry(QLatin1String("resourceName") + n, c->resource_name.constData());
    cg.writeEntry(QLatin1String("resourceClass") + n, c->resource_class.constData());
    cg.writeEntry(
        QLatin1String("geometry") + n,
        QRect(win::x11::calculate_gravitation(c, true), win::frame_to_client_size(c, c->size())));
    cg.writeEntry(QLatin1String("restore") + n, c->restore_geometries.maximize);
    cg.writeEntry(QLatin1String("fsrestore") + n, c->restore_geometries.maximize);
    cg.writeEntry(QLatin1String("maximize") + n, static_cast<int>(c->maximizeMode()));
    cg.writeEntry(QLatin1String("fullscreen") + n, static_cast<int>(c->control->fullscreen()));
    cg.writeEntry(QLatin1String("desktop") + n, c->desktop());

    // the config entry is called "iconified" for back. comp. reasons
    // (kconf_update script for updating session files would be too complicated)
    cg.writeEntry(QLatin1String("iconified") + n, c->control->minimized());
    cg.writeEntry(QLatin1String("opacity") + n, c->opacity());

    // the config entry is called "sticky" for back. comp. reasons
    cg.writeEntry(QLatin1String("sticky") + n, c->isOnAllDesktops());

    // the config entry is called "staysOnTop" for back. comp. reasons
    cg.writeEntry(QLatin1String("staysOnTop") + n, c->control->keep_above());
    cg.writeEntry(QLatin1String("keepBelow") + n, c->control->keep_below());
    cg.writeEntry(QLatin1String("skipTaskbar") + n, c->control->original_skip_taskbar());
    cg.writeEntry(QLatin1String("skipPager") + n, c->control->skip_pager());
    cg.writeEntry(QLatin1String("skipSwitcher") + n, c->control->skip_switcher());

    // not really just set by user, but name kept for back. comp. reasons
    cg.writeEntry(QLatin1String("userNoBorder") + n, c->user_no_border);
    cg.writeEntry(QLatin1String("windowType") + n, windowTypeToTxt(c->windowType()));
    cg.writeEntry(QLatin1String("shortcut") + n, c->control->shortcut().toString());
    cg.writeEntry(QLatin1String("stackingOrder") + n,
                  static_cast<int>(index_of(stacking_order->pre_stack, c)));
}

void space::storeSubSession(const QString& name, QSet<QByteArray> sessionIds)
{
    // TODO clear it first
    KConfigGroup cg(KSharedConfig::openConfig(), QLatin1String("SubSession: ") + name);
    int count = 0;
    int active_client = -1;

    for (auto const& window : m_windows) {
        if (!window->control) {
            continue;
        }

        auto x11_client = qobject_cast<win::x11::window*>(window);
        if (!x11_client) {
            continue;
        }
        if (x11_client->windowType() > NET::Splash) {
            continue;
        }

        QByteArray sessionId = x11_client->sessionId();
        QByteArray wmCommand = x11_client->wmCommand();
        if (sessionId.isEmpty()) {
            // remember also applications that are not XSMP capable
            // and use the obsolete WM_COMMAND / WM_SAVE_YOURSELF
            if (wmCommand.isEmpty()) {
                continue;
            }
        }
        if (!sessionIds.contains(sessionId)) {
            continue;
        }

        qCDebug(KWIN_CORE) << "storing" << sessionId;
        count++;

        if (x11_client->control->active()) {
            active_client = count;
        }
        storeClient(cg, count, x11_client);
    }

    cg.writeEntry("count", count);
    cg.writeEntry("active", active_client);
    // cg.writeEntry( "desktop", currentDesktop());
}

/**
 * Loads the session information from the config file.
 *
 * @see storeSession
 */
void space::loadSessionInfo(const QString& sessionName)
{
    session.clear();
    KConfigGroup cg(sessionConfig(sessionName, QString()), "Session");
    addSessionInfo(cg);
}

void space::addSessionInfo(KConfigGroup& cg)
{
    m_initialDesktop = cg.readEntry("desktop", 1);
    int count = cg.readEntry("count", 0);
    int active_client = cg.readEntry("active", 0);

    for (int i = 1; i <= count; i++) {
        QString n = QString::number(i);
        auto info = new win::session_info;
        session.push_back(info);
        info->sessionId = cg.readEntry(QLatin1String("sessionId") + n, QString()).toLatin1();
        info->windowRole = cg.readEntry(QLatin1String("windowRole") + n, QString()).toLatin1();
        info->wmCommand = cg.readEntry(QLatin1String("wmCommand") + n, QString()).toLatin1();
        info->resourceName = cg.readEntry(QLatin1String("resourceName") + n, QString()).toLatin1();
        info->resourceClass
            = cg.readEntry(QLatin1String("resourceClass") + n, QString()).toLower().toLatin1();
        info->geometry = cg.readEntry(QLatin1String("geometry") + n, QRect());
        info->restore = cg.readEntry(QLatin1String("restore") + n, QRect());
        info->fsrestore = cg.readEntry(QLatin1String("fsrestore") + n, QRect());
        info->maximized = cg.readEntry(QLatin1String("maximize") + n, 0);
        info->fullscreen = cg.readEntry(QLatin1String("fullscreen") + n, 0);
        info->desktop = cg.readEntry(QLatin1String("desktop") + n, 0);
        info->minimized = cg.readEntry(QLatin1String("iconified") + n, false);
        info->opacity = cg.readEntry(QLatin1String("opacity") + n, 1.0);
        info->onAllDesktops = cg.readEntry(QLatin1String("sticky") + n, false);
        info->keepAbove = cg.readEntry(QLatin1String("staysOnTop") + n, false);
        info->keepBelow = cg.readEntry(QLatin1String("keepBelow") + n, false);
        info->skipTaskbar = cg.readEntry(QLatin1String("skipTaskbar") + n, false);
        info->skipPager = cg.readEntry(QLatin1String("skipPager") + n, false);
        info->skipSwitcher = cg.readEntry(QLatin1String("skipSwitcher") + n, false);
        info->noBorder = cg.readEntry(QLatin1String("userNoBorder") + n, false);
        info->windowType = txtToWindowType(
            cg.readEntry(QLatin1String("windowType") + n, QString()).toLatin1().constData());
        info->shortcut = cg.readEntry(QLatin1String("shortcut") + n, QString());
        info->active = (active_client == i);
        info->stackingOrder = cg.readEntry(QLatin1String("stackingOrder") + n, -1);
    }
}

void space::loadSubSessionInfo(const QString& name)
{
    KConfigGroup cg(KSharedConfig::openConfig(), QLatin1String("SubSession: ") + name);
    addSessionInfo(cg);
}

static bool sessionInfoWindowTypeMatch(win::x11::window* c, win::session_info* info)
{
    if (info->windowType == -2) {
        // undefined (not really part of NET::WindowType)
        return !win::is_special_window(c);
    }
    return info->windowType == c->windowType();
}

/**
 * Returns a SessionInfo for client \a c. The returned session
 * info is removed from the storage. It's up to the caller to delete it.
 *
 * This function is called when a new window is mapped and must be managed.
 * We try to find a matching entry in the session.
 *
 * May return 0 if there's no session info for the client.
 */
win::session_info* space::takeSessionInfo(win::x11::window* c)
{
    win::session_info* realInfo = nullptr;
    QByteArray sessionId = c->sessionId();
    QByteArray windowRole = c->windowRole();
    QByteArray wmCommand = c->wmCommand();
    auto const& resourceName = c->resource_name;
    auto const& resourceClass = c->resource_class;

    // First search ``session''
    if (!sessionId.isEmpty()) {
        // look for a real session managed client (algorithm suggested by ICCCM)
        for (auto const& info : session) {
            if (realInfo)
                break;
            if (info->sessionId == sessionId && sessionInfoWindowTypeMatch(c, info)) {
                if (!windowRole.isEmpty()) {
                    if (info->windowRole == windowRole) {
                        realInfo = info;
                        remove_all(session, info);
                    }
                } else {
                    if (info->windowRole.isEmpty() && info->resourceName == resourceName
                        && info->resourceClass == resourceClass) {
                        realInfo = info;
                        remove_all(session, info);
                    }
                }
            }
        }
    } else {
        // look for a sessioninfo with matching features.
        for (auto const& info : session) {
            if (realInfo)
                break;
            if (info->resourceName == resourceName && info->resourceClass == resourceClass
                && sessionInfoWindowTypeMatch(c, info)) {
                if (wmCommand.isEmpty() || info->wmCommand == wmCommand) {
                    realInfo = info;
                    remove_all(session, info);
                }
            }
        }
    }
    return realInfo;
}

}
