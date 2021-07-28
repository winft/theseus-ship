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
// own
#include "workspace.h"
// kwin libs
#include <kwinglplatform.h>
// kwin
#ifdef KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif
#include "atoms.h"
#include "composite.h"
#include "cursor.h"
#include "dbusinterface.h"
#include "effects.h"
#include "moving_client_x11_filter.h"
#include "killwindow.h"
#include "outline.h"
#include "rules/rule_book.h"
#include "rules/rules.h"
#include "screenedge.h"
#include "screens.h"
#include "platform.h"
#include "scripting/scripting.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "useractions.h"
#include "virtualdesktops.h"
#include "was_user_interaction_x11_filter.h"
#include "wayland_server.h"
#include "xcbutils.h"
#include "main.h"
#include "decorations/decorationbridge.h"

#include "win/appmenu.h"
#include "win/controlling.h"
#include "win/focuschain.h"
#include "win/input.h"
#include "win/internal_client.h"
#include "win/layers.h"
#include "win/remnant.h"
#include "win/setup.h"
#include "win/space.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/util.h"

#include "win/wayland/window.h"
#include "win/wayland/xdg_activation.h"
#include "win/x11/control.h"
#include "win/x11/group.h"
#include "win/x11/netinfo.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/syncalarmx11filter.h"
#include "win/x11/transient.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window.h"

#include <Wrapland/Server/subcompositor.h>

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KStartupInfo>
// Qt
#include <QtConcurrentRun>
// std
#include <memory>

namespace KWin
{

X11EventFilterContainer::X11EventFilterContainer(X11EventFilter *filter)
    : m_filter(filter)
{
}

X11EventFilter *X11EventFilterContainer::filter() const
{
    return m_filter;
}

ColorMapper::ColorMapper(QObject *parent)
    : QObject(parent)
    , m_default(defaultScreen()->default_colormap)
    , m_installed(defaultScreen()->default_colormap)
{
}

ColorMapper::~ColorMapper()
{
}

void ColorMapper::update()
{
    xcb_colormap_t cmap = m_default;
    if (auto c = dynamic_cast<win::x11::window*>(Workspace::self()->activeClient())) {
        if (c->colormap != XCB_COLORMAP_NONE) {
            cmap = c->colormap;
        }
    }
    if (cmap != m_installed) {
        xcb_install_colormap(connection(), cmap);
        m_installed = cmap;
    }
}

Workspace* Workspace::_self = nullptr;

Workspace::Workspace()
    : QObject(nullptr)
    , stacking_order(new win::stacking_order)
    , x_stacking_tree(std::make_unique<win::x11::stacking_tree>())
    , m_userActionsMenu(new UserActionsMenu(this))
    , m_sessionManager(new SessionManager(this))
{
    // For invoke methods of UserActionsMenu.
    qRegisterMetaType<Toplevel*>();

    // If KWin was already running it saved its configuration after loosing the selection -> Reread
    QFuture<void> reparseConfigFuture = QtConcurrent::run(options, &Options::reparseConfiguration);

    win::ApplicationMenu::create(this);

    _self = this;

#ifdef KWIN_BUILD_ACTIVITIES
    Activities *activities = nullptr;
    if (kwinApp()->usesKActivities()) {
        activities = Activities::create(this);
    }
    if (activities) {
        connect(activities, &Activities::currentChanged, this, &Workspace::updateCurrentActivity);
    }
#endif

    // PluginMgr needs access to the config file, so we need to wait for it for finishing
    reparseConfigFuture.waitForFinished();

    options->loadConfig();
    options->loadCompositingConfig(false);

    m_quickTileCombineTimer = new QTimer(this);
    m_quickTileCombineTimer->setSingleShot(true);

    RuleBook::create(this)->load();

    Q_ASSERT(Screens::self());
    ScreenEdges::create(this);

    // VirtualDesktopManager needs to be created prior to init shortcuts
    // and prior to TabBox, due to TabBox connecting to signals
    // actual initialization happens in init()
    VirtualDesktopManager::create(this);
    //dbus interface
    new VirtualDesktopManagerDBusInterface(VirtualDesktopManager::self());

#ifdef KWIN_BUILD_TABBOX
    // need to create the tabbox before compositing scene is setup
    TabBox::TabBox::create(this);
#endif

    if (Compositor::self()) {
        m_compositor = Compositor::self();
    } else {
        Q_ASSERT(kwinApp()->operationMode() == Application::OperationMode::OperationModeX11);
        m_compositor = X11Compositor::create(this);
    }
    connect(this, &Workspace::currentDesktopChanged, m_compositor, &Compositor::addRepaintFull);
    connect(m_compositor, &QObject::destroyed, this, [this] { m_compositor = nullptr; });

    auto decorationBridge = Decoration::DecorationBridge::create(this);
    decorationBridge->init();
    connect(this, &Workspace::configChanged, decorationBridge, &Decoration::DecorationBridge::reconfigure);

    connect(m_sessionManager, &SessionManager::loadSessionRequested, this, &Workspace::loadSessionInfo);

    connect(m_sessionManager, &SessionManager::prepareSessionSaveRequested, this, [this](const QString &name) {
        storeSession(name, SMSavePhase0);
    });
    connect(m_sessionManager, &SessionManager::finishSessionSaveRequested, this, [this](const QString &name) {
        storeSession(name, SMSavePhase2);
    });

    new DBusInterface(this);

    Outline::create(this);

    initShortcuts();

    init();
}

void Workspace::init()
{
    KSharedConfigPtr config = kwinApp()->config();
    Screens *screens = Screens::self();
    // get screen support
    connect(screens, &Screens::changed, this, &Workspace::desktopResized);
    screens->setConfig(config);
    screens->reconfigure();
    connect(options, &Options::configChanged, screens, &Screens::reconfigure);
    ScreenEdges *screenEdges = ScreenEdges::self();
    screenEdges->setConfig(config);
    screenEdges->init();
    connect(options, &Options::configChanged, screenEdges, &ScreenEdges::reconfigure);
    connect(VirtualDesktopManager::self(), &VirtualDesktopManager::layoutChanged, screenEdges, &ScreenEdges::updateLayout);
    connect(this, &Workspace::clientActivated, screenEdges, &ScreenEdges::checkBlocking);

    auto *focusChain = win::FocusChain::create(this);
    connect(this, &Workspace::clientRemoved, focusChain, &win::FocusChain::remove);
    connect(this, &Workspace::clientActivated, focusChain, &win::FocusChain::setActiveClient);
    connect(VirtualDesktopManager::self(), &VirtualDesktopManager::countChanged, focusChain, &win::FocusChain::resize);
    connect(VirtualDesktopManager::self(), &VirtualDesktopManager::currentChanged, focusChain, &win::FocusChain::setCurrentDesktop);
    connect(options, &Options::separateScreenFocusChanged, focusChain, &win::FocusChain::setSeparateScreenFocus);
    focusChain->setSeparateScreenFocus(options->isSeparateScreenFocus());

    // create VirtualDesktopManager and perform dependency injection
    VirtualDesktopManager *vds = VirtualDesktopManager::self();
    connect(vds, &VirtualDesktopManager::desktopRemoved, this,
        [this](KWin::VirtualDesktop *desktop) {
            //Wayland
            if (kwinApp()->operationMode() == Application::OperationModeWaylandOnly ||
                kwinApp()->operationMode() == Application::OperationModeXwayland) {
                for (auto const& client : m_allClients) {
                    if (!client->desktops().contains(desktop)) {
                        continue;
                    }
                    if (client->desktops().count() > 1) {
                        win::leave_desktop(client, desktop);
                    } else {
                        sendClientToDesktop(client,
                            qMin(desktop->x11DesktopNumber(),
                                 VirtualDesktopManager::self()->count()), true);
                    }
                }
            //X11
            } else {
                for (auto const& client : m_allClients) {
                    if (!client->isOnAllDesktops() &&
                            (client->desktop() > static_cast<int>(VirtualDesktopManager::self()->count()))) {
                        sendClientToDesktop(client, VirtualDesktopManager::self()->count(), true);
                    }
                }
            }
        }
    );

    connect(vds, &VirtualDesktopManager::countChanged, this, &Workspace::slotDesktopCountChanged);
    connect(vds, &VirtualDesktopManager::currentChanged, this, &Workspace::slotCurrentDesktopChanged);
    vds->setNavigationWrappingAround(options->isRollOverDesktops());
    connect(options, &Options::rollOverDesktopsChanged, vds, &VirtualDesktopManager::setNavigationWrappingAround);
    vds->setConfig(config);

    // positioning object needs to be created before the virtual desktops are loaded.
    vds->load();
    vds->updateLayout();
    //makes sure any autogenerated id is saved, necessary as in case of xwayland, load will be called 2 times
    // load is needed to be called again when starting xwayalnd to sync to RootInfo, see BUG 385260
    vds->save();

    if (!VirtualDesktopManager::self()->setCurrent(m_initialDesktop))
        VirtualDesktopManager::self()->setCurrent(1);

    reconfigureTimer.setSingleShot(true);
    updateToolWindowsTimer.setSingleShot(true);

    connect(&reconfigureTimer, &QTimer::timeout, this, &Workspace::slotReconfigure);
    connect(&updateToolWindowsTimer, &QTimer::timeout, this, &Workspace::slotUpdateToolWindows);

    // TODO: do we really need to reconfigure everything when fonts change?
    // maybe just reconfigure the decorations? Move this into libkdecoration?
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KDEPlatformTheme"),
                                          QStringLiteral("org.kde.KDEPlatformTheme"),
                                          QStringLiteral("refreshFonts"),
                                          this, SLOT(reconfigure()));

    active_client = nullptr;
    connect(stacking_order, &win::stacking_order::changed, this, [this] {
        if (active_client) {
            active_client->control->update_mouse_grab();
        }
    });
    initWithX11();

    Scripting::create(this);

    if (auto w = waylandServer()) {
        activation.reset(new win::wayland::xdg_activation);
        connect(this, &Workspace::clientActivated, this, [this] {
            if (activeClient()) {
                activation->clear();
            }
        });

        connect(w, &WaylandServer::window_added, this,
            [this] (win::wayland::window* window) {
            assert(!contains(m_windows, window));

            if (window->control && !window->layer_surface) {
                setupClientConnections(window);
                window->updateDecoration(false);
                win::update_layer(window);

                auto const area = clientArea(PlacementArea, Screens::self()->current(),
                                             window->desktop());
                auto placementDone = false;

                if (window->isInitialPositionSet()) {
                    placementDone = true;
                }
                if (window->control->fullscreen()) {
                    placementDone = true;
                }
                if (window->maximizeMode() == win::maximize_mode::full) {
                    placementDone = true;
                }
                if (window->control->rules().checkPosition(invalidPoint, true) != invalidPoint) {
                    placementDone = true;
                }
                if (!placementDone) {
                    window->placeIn(area);
                }

                m_allClients.push_back(window);
            }

            m_windows.push_back(window);

            if (!contains(stacking_order->pre_stack, window)) {
                // Raise if it hasn't got any stacking position yet.
                stacking_order->pre_stack.push_back(window);
            }
            if (!contains(stacking_order->sorted(), window)) {
                // It'll be updated later, and updateToolWindows() requires window to be in
                // stacking_order.
                stacking_order->win_stack.push_back(window);
            }

            x_stacking_tree->mark_as_dirty();
            stacking_order->update(true);

            if (window->control) {
                updateClientArea();

                if (window->wantsInput() && !window->control->minimized()) {
                    activateClient(window);
                }

                updateTabbox();

                connect(window, &win::wayland::window::windowShown, this,
                    [this, window] {
                        win::update_layer(window);
                        x_stacking_tree->mark_as_dirty();
                        stacking_order->update(true);
                        updateClientArea();
                        if (window->wantsInput()) {
                            activateClient(window);
                        }
                    }
                );
                connect(window, &win::wayland::window::windowHidden, this,
                    [this] {
                        // TODO: update tabbox if it's displayed
                        x_stacking_tree->mark_as_dirty();
                        stacking_order->update(true);
                        updateClientArea();
                    }
                );
            }
        });
        connect(w, &WaylandServer::window_removed, this,
            [this] (win::wayland::window* window) {
            remove_all(m_windows, window);

            if (window->control) {
                remove_all(m_allClients, window);
                if (window == most_recently_raised) {
                    most_recently_raised = nullptr;
                }
                if (window == delayfocus_client) {
                    cancelDelayFocus();
                }
                if (window == last_active_client) {
                    last_active_client = nullptr;
                }
                if (window == client_keys_client) {
                    setupWindowShortcutDone(false);
                }
                if (!window->control->shortcut().isEmpty()) {
                    // Remove from client_keys.
                    win::set_shortcut(window, QString());
                }
                clientHidden(window);
                Q_EMIT clientRemoved(window);
            }

            x_stacking_tree->mark_as_dirty();
            stacking_order->update(true);

            if (window->control) {
                updateClientArea();
                updateTabbox();
            }
        });
    }

    // SELI TODO: This won't work with unreasonable focus policies,
    // and maybe in rare cases also if the selected client doesn't
    // want focus
    workspaceInit = false;

    // broadcast that Workspace is ready, but first process all events.
    QMetaObject::invokeMethod(this, "workspaceInitialized", Qt::QueuedConnection);

    // TODO: ungrabXServer()
}

void Workspace::initWithX11()
{
    if (!kwinApp()->x11Connection()) {
        connect(kwinApp(), &Application::x11ConnectionChanged, this, &Workspace::initWithX11, Qt::UniqueConnection);
        return;
    }
    disconnect(kwinApp(), &Application::x11ConnectionChanged, this, &Workspace::initWithX11);

    atoms->retrieveHelpers();

    // first initialize the extensions
    Xcb::Extensions::self();
    ColorMapper *colormaps = new ColorMapper(this);
    connect(this, &Workspace::clientActivated, colormaps, &ColorMapper::update);

    // Call this before XSelectInput() on the root window
    startup = new KStartupInfo(
        KStartupInfo::DisableKWinModule | KStartupInfo::AnnounceSilenceChanges, this);

    // Select windowmanager privileges
    selectWmInputEventMask();

    // Compatibility
    int32_t data = 1;

    xcb_change_property(connection(), XCB_PROP_MODE_APPEND, rootWindow(), atoms->kwin_running,
                        atoms->kwin_running, 32, 1, &data);

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        m_wasUserInteractionFilter.reset(new WasUserInteractionX11Filter);
        m_movingClientFilter.reset(new MovingClientX11Filter);
    }
    if (Xcb::Extensions::self()->isSyncAvailable()) {
        m_syncAlarmFilter.reset(new win::x11::SyncAlarmX11Filter);
    }
    updateXTime(); // Needed for proper initialization of user_time in Client ctor

    const uint32_t nullFocusValues[] = {true};
    m_nullFocus.reset(new Xcb::Window(QRect(-1, -1, 1, 1), XCB_WINDOW_CLASS_INPUT_ONLY, XCB_CW_OVERRIDE_REDIRECT, nullFocusValues));
    m_nullFocus->map();

    auto *rootInfo = win::x11::RootInfo::create();
    const auto vds = VirtualDesktopManager::self();
    vds->setRootInfo(rootInfo);
    rootInfo->activate();

    // TODO: only in X11 mode
    // Extra NETRootInfo instance in Client mode is needed to get the values of the properties
    NETRootInfo client_info(connection(), NET::ActiveWindow | NET::CurrentDesktop);
    if (!qApp->isSessionRestored()) {
        m_initialDesktop = client_info.currentDesktop();
        vds->setCurrent(m_initialDesktop);
    }

    // TODO: better value
    rootInfo->setActiveWindow(XCB_WINDOW_NONE);
    focusToNull();

    if (!qApp->isSessionRestored())
        ++block_focus; // Because it will be set below

    {
        // Begin updates blocker block
        Blocker blocker(stacking_order);

        Xcb::Tree tree(rootWindow());
        xcb_window_t *wins = xcb_query_tree_children(tree.data());

        QVector<Xcb::WindowAttributes> windowAttributes(tree->children_len);
        QVector<Xcb::WindowGeometry> windowGeometries(tree->children_len);

        // Request the attributes and geometries of all toplevel windows
        for (int i = 0; i < tree->children_len; i++) {
            windowAttributes[i] = Xcb::WindowAttributes(wins[i]);
            windowGeometries[i] = Xcb::WindowGeometry(wins[i]);
        }

        // Get the replies
        for (int i = 0; i < tree->children_len; i++) {
            Xcb::WindowAttributes attr(windowAttributes.at(i));

            if (attr.isNull()) {
                continue;
            }

            if (attr->override_redirect) {
                if (attr->map_state == XCB_MAP_STATE_VIEWABLE &&
                    attr->_class != XCB_WINDOW_CLASS_INPUT_ONLY)
                    // ### This will request the attributes again
                    createUnmanaged(wins[i]);
            } else if (attr->map_state != XCB_MAP_STATE_UNMAPPED) {
                if (Application::wasCrash()) {
                    fixPositionAfterCrash(wins[i], windowGeometries.at(i).data());
                }

                // ### This will request the attributes again
                createClient(wins[i], true);
            }
        }

        // Propagate clients, will really happen at the end of the updates blocker block
        stacking_order->update(true);

        saveOldScreenSizes();
        updateClientArea();

        // NETWM spec says we have to set it to (0,0) if we don't support it
        NETPoint* viewports = new NETPoint[VirtualDesktopManager::self()->count()];
        rootInfo->setDesktopViewport(VirtualDesktopManager::self()->count(), *viewports);
        delete[] viewports;
        QRect geom;
        for (int i = 0; i < screens()->count(); i++) {
            geom |= screens()->geometry(i);
        }
        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        rootInfo->setDesktopGeometry(desktop_geometry);
        setShowingDesktop(false);

    } // End updates blocker block

    // TODO: only on X11?
    Toplevel* new_active_client = nullptr;
    if (!qApp->isSessionRestored()) {
        --block_focus;
        new_active_client = findClient(win::x11::predicate_match::window, client_info.activeWindow());
    }
    if (new_active_client == nullptr
            && activeClient() == nullptr && should_get_focus.size() == 0) {
        // No client activated in manage()
        if (new_active_client == nullptr)
            new_active_client = win::top_client_on_desktop(
                this, VirtualDesktopManager::self()->current(), -1);
        if (new_active_client == nullptr) {
            new_active_client = win::find_desktop(
                this, true, VirtualDesktopManager::self()->current());
        }
    }
    if (new_active_client != nullptr)
        activateClient(new_active_client);
}

Workspace::~Workspace()
{
    stacking_order->lock();

    // TODO: grabXServer();

    // Use stacking_order, so that kwin --replace keeps stacking order
    auto const stack = stacking_order->sorted();
    // "mutex" the stackingorder, since anything trying to access it from now on will find
    // many dangeling pointers and crash
    stacking_order->win_stack.clear();

    for (auto it = stack.cbegin(), end = stack.cend(); it != end; ++it) {
        auto c = qobject_cast<win::x11::window*>(const_cast<Toplevel*>(*it));
        if (!c) {
            continue;
        }
        // Only release the window
        c->release_window(true);
        // No removeClient() is called, it does more than just removing.
        // However, remove from some lists to e.g. prevent performTransiencyCheck()
        // from crashing.
        remove_all(m_allClients, c);
        remove_all(m_windows, c);
    }

    win::x11::window::cleanupX11();

    if (waylandServer()) {
        auto const windows = waylandServer()->windows;
        for (auto win : windows) {
            win->destroy();
            remove_all(m_windows, win);
        }
    }

    for (auto const& unmanaged : unmanagedList()) {
        win::x11::release_unmanaged(unmanaged, ReleaseReason::KWinShutsDown);
        remove_all(m_windows, unmanaged);
    }

    for (auto const& window : m_windows) {
        if (auto internal = qobject_cast<win::InternalClient*>(window)) {
            internal->destroyClient();
            remove_all(m_windows, internal);
        }
    }

    for (auto const& window : m_windows) {
        if (auto win = qobject_cast<win::wayland::window*>(window)) {
            win->destroy();
            remove_all(m_windows, win);
        }
    }

    // At this point only remnants are remaining.
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        assert((*it)->remnant());
        Q_EMIT deletedRemoved(*it);
        it = m_windows.erase(it);
    }

    assert(m_windows.empty());

    if (auto c = kwinApp()->x11Connection()) {
        xcb_delete_property(c, kwinApp()->x11RootWindow(), atoms->kwin_running);
    }

    delete stacking_order;

    delete RuleBook::self();
    kwinApp()->config()->sync();

    win::x11::RootInfo::destroy();
    delete startup;
    delete client_keys_dialog;
    foreach (SessionInfo * s, session)
    delete s;

    // TODO: ungrabXServer();

    Xcb::Extensions::destroy();
    _self = nullptr;
}

void Workspace::setupClientConnections(Toplevel* window)
{
    connect(window, &Toplevel::needsRepaint, m_compositor, [window] {
        Compositor::self()->schedule_repaint(window);
    });
    connect(window, &Toplevel::desktopPresenceChanged, this, &Workspace::desktopPresenceChanged);
    connect(window, &Toplevel::minimizedChanged, this, std::bind(&Workspace::clientMinimizedChanged, this, window));
}

win::x11::window* Workspace::createClient(xcb_window_t w, bool is_mapped)
{
    Blocker blocker(stacking_order);

    auto c = new win::x11::window();
    setupClientConnections(c);

    if (X11Compositor *compositor = X11Compositor::self()) {
        connect(c, &win::x11::window::blockingCompositingChanged, compositor, &X11Compositor::updateClientCompositeBlocking);
    }
    connect(c, &win::x11::window::clientFullScreenSet, ScreenEdges::self(), &ScreenEdges::checkBlocking);
    if (!win::x11::take_control(c, w, is_mapped)) {
        delete c;
        return nullptr;
    }
    addClient(c);
    return c;
}

Toplevel* Workspace::createUnmanaged(xcb_window_t w)
{
    if (X11Compositor *compositor = X11Compositor::self()) {
        if (compositor->checkForOverlayWindow(w)) {
            return nullptr;
        }
    }
    auto c = new Toplevel();
    win::x11::setup_unmanaged(c);
    if (!win::x11::track(c, w)) {
        delete c;
        return nullptr;
    }
    connect(c, &Toplevel::needsRepaint, m_compositor, [c] {
        Compositor::self()->schedule_repaint(c);
    });
    addUnmanaged(c);
    Q_EMIT unmanagedAdded(c);
    return c;
}

void Workspace::addClient(win::x11::window* c)
{
    auto grp = findGroup(c->xcb_window());

    emit clientAdded(c);

    if (grp != nullptr)
        grp->gotLeader(c);

    if (win::is_desktop(c)) {
        if (active_client == nullptr && should_get_focus.empty() && c->isOnCurrentDesktop()) {
            // TODO: Make sure desktop is active after startup if there's no other window active
            request_focus(c);
        }
    } else {
        win::FocusChain::self()->update(c, win::FocusChain::Update);
    }

    m_windows.push_back(c);
    m_allClients.push_back(c);

    if (!contains(stacking_order->pre_stack, c)) {
        // Raise if it hasn't got any stacking position yet
        stacking_order->pre_stack.push_back(c);
    }
    if (!contains(stacking_order->sorted(), c)) {
        // It'll be updated later, and updateToolWindows() requires c to be in stacking_order.
        stacking_order->win_stack.push_back(c);
    }
    x_stacking_tree->mark_as_dirty();
    updateClientArea(); // This cannot be in manage(), because the client got added only now
    win::update_layer(c);
    if (win::is_desktop(c)) {
        win::raise_window(this, c);
        // If there's no active client, make this desktop the active one
        if (activeClient() == nullptr && should_get_focus.size() == 0)
            activateClient(
                win::find_desktop(this, true, VirtualDesktopManager::self()->current()));
    }
    win::x11::check_active_modal<win::x11::window>();
    checkTransients(c);
    stacking_order->update(true);   // Propagate new client
    if (win::is_utility(c) || win::is_menu(c) || win::is_toolbar(c)) {
        win::update_tool_windows(this, true);
    }
    updateTabbox();
}

void Workspace::addUnmanaged(Toplevel *c)
{
    m_windows.push_back(c);
    x_stacking_tree->mark_as_dirty();
}

/**
 * Destroys the client \a c
 */
void Workspace::removeClient(win::x11::window* c)
{
    if (c == active_popup_client)
        closeActivePopup();
    if (m_userActionsMenu->isMenuClient(c)) {
        m_userActionsMenu->close();
    }

    if (client_keys_client == c)
        setupWindowShortcutDone(false);
    if (!c->control->shortcut().isEmpty()) {
        // Remove from client_keys.
        win::set_shortcut(c, QString());

        // Needed, since this is otherwise delayed by setShortcut() and wouldn't run
        clientShortcutUpdated(c);
    }

    assert(contains(m_allClients, c));
    // TODO: if marked client is removed, notify the marked list
    remove_all(m_allClients, c);
    remove_all(m_windows, c);
    x_stacking_tree->mark_as_dirty();
    remove_all(attention_chain, c);

    auto group = findGroup(c->xcb_window());
    if (group != nullptr)
        group->lostLeader();

    if (c == most_recently_raised) {
        most_recently_raised = nullptr;
    }
    remove_all(should_get_focus, c);
    Q_ASSERT(c != active_client);
    if (c == last_active_client)
        last_active_client = nullptr;
    if (c == delayfocus_client)
        cancelDelayFocus();

    emit clientRemoved(c);

    stacking_order->update(true);
    updateClientArea();
    updateTabbox();
}

void Workspace::removeUnmanaged(Toplevel* window)
{
    Q_ASSERT(contains(m_windows, window));
    remove_all(m_windows, window);
    Q_EMIT unmanagedRemoved(window);
    x_stacking_tree->mark_as_dirty();
}

void Workspace::addDeleted(Toplevel* c, Toplevel* orig)
{
    assert(!contains(m_windows, c));

    m_remnant_count++;
    m_windows.push_back(c);

    auto const unconstraintedIndex = index_of(stacking_order->pre_stack, orig);
    if (unconstraintedIndex != -1) {
        stacking_order->pre_stack.at(unconstraintedIndex) = c;
    } else {
        stacking_order->pre_stack.push_back(c);
    }
    auto const index = index_of(stacking_order->sorted(), orig);
    if (index != -1) {
        stacking_order->win_stack.at(index) = c;
    } else {
        stacking_order->win_stack.push_back(c);
    }
    x_stacking_tree->mark_as_dirty();
    connect(c, &Toplevel::needsRepaint, m_compositor, [c] {
        Compositor::self()->schedule_repaint(c);
    });
}

void Workspace::removeDeleted(Toplevel* window)
{
    assert(contains(m_windows, window));

    emit deletedRemoved(window);
    m_remnant_count--;

    remove_all(m_windows, window);
    remove_all(stacking_order->pre_stack, window);
    remove_all(stacking_order->win_stack, window);

    x_stacking_tree->mark_as_dirty();

    if (auto compositor = X11Compositor::self(); compositor && window->remnant()->control) {
        compositor->updateClientCompositeBlocking();
    }
}

void Workspace::stopUpdateToolWindowsTimer()
{
    updateToolWindowsTimer.stop();
}

void Workspace::resetUpdateToolWindowsTimer()
{
    updateToolWindowsTimer.start(200);
}

void Workspace::slotUpdateToolWindows()
{
    win::update_tool_windows(this, true);
}

void Workspace::slotReloadConfig()
{
    reconfigure();
}

void Workspace::reconfigure()
{
    reconfigureTimer.start(200);
}

/**
 * Reread settings
 */

void Workspace::slotReconfigure()
{
    qCDebug(KWIN_CORE) << "Workspace::slotReconfigure()";
    reconfigureTimer.stop();

    bool borderlessMaximizedWindows = options->borderlessMaximizedWindows();

    kwinApp()->config()->reparseConfiguration();
    options->updateSettings();

    emit configChanged();
    m_userActionsMenu->discard();
    win::update_tool_windows(this, true);

    RuleBook::self()->load();
    for (auto it = m_allClients.begin();
            it != m_allClients.end();
            ++it) {
        win::setup_rules(*it, true);
        (*it)->applyWindowRules();
        RuleBook::self()->discardUsed(*it, false);
    }

    if (borderlessMaximizedWindows != options->borderlessMaximizedWindows() &&
            !options->borderlessMaximizedWindows()) {
        // in case borderless maximized windows option changed and new option
        // is to have borders, we need to unset the borders for all maximized windows
        for (auto it = m_allClients.begin();
                it != m_allClients.end();
                ++it) {
            if ((*it)->maximizeMode() == win::maximize_mode::full)
                (*it)->checkNoBorder();
        }
    }
}

void Workspace::slotCurrentDesktopChanged(uint oldDesktop, uint newDesktop)
{
    closeActivePopup();
    ++block_focus;
    Blocker blocker(stacking_order);
    win::update_client_visibility_on_desktop_change(this, newDesktop);
    // Restore the focus on this desktop
    --block_focus;

    activateClientOnNewDesktop(newDesktop);
    emit currentDesktopChanged(oldDesktop, movingClient);
}

void Workspace::activateClientOnNewDesktop(uint desktop)
{
    Toplevel* c = nullptr;
    if (options->focusPolicyIsReasonable()) {
        c = findClientToActivateOnDesktop(desktop);
    }
    // If "unreasonable focus policy" and active_client is on_all_desktops and
    // under mouse (Hence == old_active_client), conserve focus.
    // (Thanks to Volker Schatz <V.Schatz at thphys.uni-heidelberg.de>)
    else if (active_client && active_client->isShown() && active_client->isOnCurrentDesktop())
        c = active_client;

    if (!c) {
        c = win::find_desktop(this, true, desktop);
    }

    if (c != active_client) {
        setActiveClient(nullptr);
    }

    if (c) {
        request_focus(c);
    } else if (auto desktop_client = win::find_desktop(this, true, desktop)) {
        request_focus(desktop_client);
    } else {
        focusToNull();
    }
}

Toplevel* Workspace::findClientToActivateOnDesktop(uint desktop)
{
    if (movingClient != nullptr && active_client == movingClient &&
        win::FocusChain::self()->contains(active_client, desktop) &&
        active_client->isShown() && active_client->isOnCurrentDesktop()) {
        // A requestFocus call will fail, as the client is already active
        return active_client;
    }
    // from actiavtion.cpp
    if (options->isNextFocusPrefersMouse()) {
        auto it = stacking_order->sorted().cend();
        while (it != stacking_order->sorted().cbegin()) {
            auto client = qobject_cast<win::x11::window*>(*(--it));
            if (!client) {
                continue;
            }

            if (!(client->isShown() && client->isOnDesktop(desktop) &&
                client->isOnCurrentActivity() && win::on_active_screen(client)))
                continue;

            if (client->frameGeometry().contains(Cursor::pos())) {
                if (!win::is_desktop(client))
                    return client;
            break; // unconditional break  - we do not pass the focus to some client below an unusable one
            }
        }
    }
    return win::FocusChain::self()->getForActivation(desktop);
}

/**
 * Updates the current activity when it changes
 * do *not* call this directly; it does not set the activity.
 *
 * Shows/Hides windows according to the stacking order
 */

void Workspace::updateCurrentActivity(const QString &new_activity)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return;
    }
    //closeActivePopup();
    ++block_focus;
    // TODO: Q_ASSERT( block_stacking_updates == 0 ); // Make sure stacking_order is up to date
    Blocker blocker(stacking_order);

    // Optimized Desktop switching: unmapping done from back to front
    // mapping done from front to back => less exposure events
    //Notify::raise((Notify::Event) (Notify::DesktopChange+new_desktop));

    for (auto it = stacking_order->sorted().cbegin();
            it != stacking_order->sorted().cend();
            ++it) {
        auto c = qobject_cast<win::x11::window*>(*it);
        if (!c) {
            continue;
        }
        if (!c->isOnActivity(new_activity) && c != movingClient && c->isOnCurrentDesktop()) {
            win::x11::update_visibility(c);
        }
    }

    // Now propagate the change, after hiding, before showing
    //rootInfo->setCurrentDesktop( currentDesktop() );

    /* TODO someday enable dragging windows to other activities
    if ( movingClient && !movingClient->isOnDesktop( new_desktop ))
        {
        movingClient->setDesktop( new_desktop );
        */

    for (int i = stacking_order->sorted().size() - 1; i >= 0 ; --i) {
        auto c = qobject_cast<win::x11::window*>(stacking_order->sorted().at(i));
        if (!c) {
            continue;
        }
        if (c->isOnActivity(new_activity)) {
            win::x11::update_visibility(c);
        }
    }

    //FIXME not sure if I should do this either
    if (showingDesktop())   // Do this only after desktop change to avoid flicker
        setShowingDesktop(false);

    // Restore the focus on this desktop
    --block_focus;
    Toplevel* c = nullptr;

    //FIXME below here is a lot of focuschain stuff, probably all wrong now
    if (options->focusPolicyIsReasonable()) {
        // Search in focus chain
        c = win::FocusChain::self()->getForActivation(VirtualDesktopManager::self()->current());
    }
    // If "unreasonable focus policy" and active_client is on_all_desktops and
    // under mouse (Hence == old_active_client), conserve focus.
    // (Thanks to Volker Schatz <V.Schatz at thphys.uni-heidelberg.de>)
    else if (active_client && active_client->isShown() && active_client->isOnCurrentDesktop() && active_client->isOnCurrentActivity())
        c = active_client;

    if (c == nullptr) {
        c = win::find_desktop(this, true, VirtualDesktopManager::self()->current());
    }

    if (c != active_client)
        setActiveClient(nullptr);

    if (c)
        request_focus(c);
    else if (auto desktop = win::find_desktop(this, true, VirtualDesktopManager::self()->current()))
        request_focus(desktop);
    else
        focusToNull();

    // Not for the very first time, only if something changed and there are more than 1 desktops

    //if ( effects != NULL && old_desktop != 0 && old_desktop != new_desktop )
    //    static_cast<EffectsHandlerImpl*>( effects )->desktopChanged( old_desktop );
    if (compositing() && m_compositor)
        m_compositor->addRepaintFull();
#else
    Q_UNUSED(new_activity)
#endif
}

void Workspace::slotDesktopCountChanged(uint previousCount, uint newCount)
{
    Q_UNUSED(previousCount)
    resetClientAreas(newCount);
}

void Workspace::resetClientAreas(uint desktopCount)
{
    // Make it +1, so that it can be accessed as [1..numberofdesktops]
    workarea.clear();
    workarea.resize(desktopCount + 1);
    restrictedmovearea.clear();
    restrictedmovearea.resize(desktopCount + 1);
    screenarea.clear();

    updateClientArea(true);
}

void Workspace::selectWmInputEventMask()
{
    uint32_t presentMask = 0;
    Xcb::WindowAttributes attr(rootWindow());
    if (!attr.isNull()) {
        presentMask = attr->your_event_mask;
    }

    Xcb::selectInput(rootWindow(),
                     presentMask |
                     XCB_EVENT_MASK_KEY_PRESS |
                     XCB_EVENT_MASK_PROPERTY_CHANGE |
                     XCB_EVENT_MASK_COLOR_MAP_CHANGE |
                     XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                     XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                     XCB_EVENT_MASK_FOCUS_CHANGE | // For NotifyDetailNone
                     XCB_EVENT_MASK_EXPOSURE
    );
}

/**
 * Sends client \a c to desktop \a desk.
 *
 * Takes care of transients as well.
 */
void Workspace::sendClientToDesktop(Toplevel* window, int desk, bool dont_activate)
{
    if ((desk < 1 && desk != NET::OnAllDesktops) ||
            desk > static_cast<int>(VirtualDesktopManager::self()->count())) {
        return;
    }
    auto old_desktop = window->desktop();
    auto was_on_desktop = window->isOnDesktop(desk) || window->isOnAllDesktops();
    win::set_desktop(window, desk);
    if (window->desktop() != desk) {
        // No change or desktop forced
        return;
    }
    desk = window->desktop(); // Client did range checking

    if (window->isOnDesktop(VirtualDesktopManager::self()->current())) {
        if (win::wants_tab_focus(window) && options->focusPolicyIsReasonable() &&
                !was_on_desktop && // for stickyness changes
                !dont_activate) {
            request_focus(window);
        } else {
            win::restack_client_under_active(this, window);
        }
    } else
        win::raise_window(this, window);

    win::check_workspace_position(window, QRect(), old_desktop );

    auto transients_stacking_order = ensureStackingOrder(window->transient()->children);
    for (auto const& transient : transients_stacking_order) {
        if (transient->control) {
            sendClientToDesktop(transient, desk, dont_activate);
        }
    }
    updateClientArea();
}

void Workspace::sendClientToScreen(Toplevel* window, int screen)
{
    win::send_to_screen(window, screen);
}

/**
 * Delayed focus functions
 */
void Workspace::delayFocus()
{
    request_focus(delayfocus_client);
    cancelDelayFocus();
}

void Workspace::requestDelayFocus(Toplevel* c)
{
    delayfocus_client = c;
    delete delayFocusTimer;
    delayFocusTimer = new QTimer(this);
    connect(delayFocusTimer, &QTimer::timeout, this, &Workspace::delayFocus);
    delayFocusTimer->setSingleShot(true);
    delayFocusTimer->start(options->delayFocusInterval());
}

void Workspace::cancelDelayFocus()
{
    delete delayFocusTimer;
    delayFocusTimer = nullptr;
}

bool Workspace::checkStartupNotification(xcb_window_t w, KStartupInfoId &id, KStartupInfoData &data)
{
    return startup->checkStartup(w, id, data) == KStartupInfo::Match;
}

/**
 * Puts the focus on a dummy window
 * Just using XSetInputFocus() with None would block keyboard input
 */
void Workspace::focusToNull()
{
    if (m_nullFocus) {
        m_nullFocus->focus();
    }
}

void Workspace::setShowingDesktop(bool showing)
{
    const bool changed = showing != showing_desktop;
    if (win::x11::rootInfo() && changed) {
        win::x11::rootInfo()->setShowingDesktop(showing);
    }
    showing_desktop = showing;

    Toplevel* topDesk = nullptr;

    { // for the blocker RAII
    Blocker blocker(stacking_order); // updateLayer & lowerClient would invalidate stacking_order
    for (int i = static_cast<int>(stacking_order->sorted().size()) - 1; i > -1; --i) {
        auto c = qobject_cast<Toplevel*>(stacking_order->sorted().at(i));
        if (c && c->isOnCurrentDesktop()) {
            if (win::is_dock(c)) {
                win::update_layer(c);
            } else if (win::is_desktop(c) && c->isShown()) {
                win::update_layer(c);
                win::lower_window(this, c);
                if (!topDesk)
                    topDesk = c;
                if (auto group = c->group()) {
                    for (auto cm : group->members()) {
                        win::update_layer(cm);
                    }
                }
            }
        }
    }
    } // ~Blocker

    if (showing_desktop && topDesk) {
        request_focus(topDesk);
    } else if (!showing_desktop && changed) {
        const auto client = win::FocusChain::self()->getForActivation(VirtualDesktopManager::self()->current());
        if (client) {
            activateClient(client);
        }
    }
    if (changed)
        emit showingDesktopChanged(showing);
}

void Workspace::disableGlobalShortcutsForClient(bool disable)
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
    for (auto& client : allClientList()) {
        client->control->update_mouse_grab();
    }
}

QString Workspace::supportInformation() const
{
    QString support;
    const QString yes = QStringLiteral("yes\n");
    const QString no = QStringLiteral("no\n");

    support.append(ki18nc("Introductory text shown in the support information.",
        "KWin Support Information:\n"
        "The following information should be used when requesting support on e.g. https://forum.kde.org.\n"
        "It provides information about the currently running instance, which options are used,\n"
        "what OpenGL driver and which effects are running.\n"
        "Please post the information provided underneath this introductory text to a paste bin service\n"
        "like https://paste.kde.org instead of pasting into support threads.\n").toString());
    support.append(QStringLiteral("\n==========================\n\n"));
    // all following strings are intended for support. They need to be pasted to e.g forums.kde.org
    // it is expected that the support will happen in English language or that the people providing
    // help understand English. Because of that all texts are not translated
    support.append(QStringLiteral("Version\n"));
    support.append(QStringLiteral("=======\n"));
    support.append(QStringLiteral("KWin version: "));
    support.append(QStringLiteral(KWIN_VERSION_STRING));
    support.append(QStringLiteral("\n"));
    support.append(QStringLiteral("Qt Version: "));
    support.append(QString::fromUtf8(qVersion()));
    support.append(QStringLiteral("\n"));
    support.append(QStringLiteral("Qt compile version: %1\n").arg(QStringLiteral(QT_VERSION_STR)));
    support.append(QStringLiteral("XCB compile version: %1\n\n").arg(QStringLiteral(XCB_VERSION_STRING)));
    support.append(QStringLiteral("Operation Mode: "));
    switch (kwinApp()->operationMode()) {
    case Application::OperationModeX11:
        support.append(QStringLiteral("X11 only"));
        break;
    case Application::OperationModeWaylandOnly:
        support.append(QStringLiteral("Wayland Only"));
        break;
    case Application::OperationModeXwayland:
        support.append(QStringLiteral("Xwayland"));
        break;
    }
    support.append(QStringLiteral("\n\n"));

    support.append(QStringLiteral("Build Options\n"));
    support.append(QStringLiteral("=============\n"));

    support.append(QStringLiteral("KWIN_BUILD_DECORATIONS: "));
#ifdef KWIN_BUILD_DECORATIONS
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("KWIN_BUILD_TABBOX: "));
#ifdef KWIN_BUILD_TABBOX
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("KWIN_BUILD_ACTIVITIES: "));
#ifdef KWIN_BUILD_ACTIVITIES
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("HAVE_PERF: "));
#if HAVE_PERF
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("HAVE_EPOXY_GLX: "));
#if HAVE_EPOXY_GLX
    support.append(yes);
#else
    support.append(no);
#endif
    support.append(QStringLiteral("\n"));

    if (auto c = kwinApp()->x11Connection()) {
        support.append(QStringLiteral("X11\n"));
        support.append(QStringLiteral("===\n"));
        auto x11setup = xcb_get_setup(c);
        support.append(QStringLiteral("Vendor: %1\n").arg(QString::fromUtf8(QByteArray::fromRawData(xcb_setup_vendor(x11setup), xcb_setup_vendor_length(x11setup)))));
        support.append(QStringLiteral("Vendor Release: %1\n").arg(x11setup->release_number));
        support.append(QStringLiteral("Protocol Version/Revision: %1/%2\n").arg(x11setup->protocol_major_version).arg(x11setup->protocol_minor_version));
        const auto extensions = Xcb::Extensions::self()->extensions();
        for (const auto &e : extensions) {
            support.append(QStringLiteral("%1: %2; Version: 0x%3\n").arg(QString::fromUtf8(e.name))
                                                                    .arg(e.present ? yes.trimmed() : no.trimmed())
                                                                    .arg(QString::number(e.version, 16)));
        }
        support.append(QStringLiteral("\n"));
    }

    if (auto bridge = Decoration::DecorationBridge::self()) {
        support.append(QStringLiteral("Decoration\n"));
        support.append(QStringLiteral("==========\n"));
        support.append(bridge->supportInformation());
        support.append(QStringLiteral("\n"));
    }
    support.append(QStringLiteral("Platform\n"));
    support.append(QStringLiteral("==========\n"));
    support.append(kwinApp()->platform()->supportInformation());
    support.append(QStringLiteral("\n"));

    support.append(QStringLiteral("Options\n"));
    support.append(QStringLiteral("=======\n"));
    const QMetaObject *metaOptions = options->metaObject();
    auto printProperty = [] (const QVariant &variant) {
        if (variant.type() == QVariant::Size) {
            const QSize &s = variant.toSize();
            return QStringLiteral("%1x%2").arg(QString::number(s.width())).arg(QString::number(s.height()));
        }
        if (QLatin1String(variant.typeName()) == QLatin1String("KWin::OpenGLPlatformInterface") ||
                QLatin1String(variant.typeName()) == QLatin1String("KWin::Options::WindowOperation")) {
            return QString::number(variant.toInt());
        }
        return variant.toString();
    };
    for (int i=0; i<metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (QLatin1String(property.name()) == QLatin1String("objectName")) {
            continue;
        }
        support.append(QStringLiteral("%1: %2\n").arg(property.name()).arg(printProperty(options->property(property.name()))));
    }
    support.append(QStringLiteral("\nScreen Edges\n"));
    support.append(QStringLiteral(  "============\n"));
    const QMetaObject *metaScreenEdges = ScreenEdges::self()->metaObject();
    for (int i=0; i<metaScreenEdges->propertyCount(); ++i) {
        const QMetaProperty property = metaScreenEdges->property(i);
        if (QLatin1String(property.name()) == QLatin1String("objectName")) {
            continue;
        }
        support.append(QStringLiteral("%1: %2\n").arg(property.name()).arg(printProperty(ScreenEdges::self()->property(property.name()))));
    }
    support.append(QStringLiteral("\nScreens\n"));
    support.append(QStringLiteral(  "=======\n"));
    support.append(QStringLiteral("Multi-Head: "));
    support.append(QStringLiteral("not supported anymore\n"));
    support.append(QStringLiteral("Active screen follows mouse: "));
    if (screens()->isCurrentFollowsMouse())
        support.append(QStringLiteral(" yes\n"));
    else
        support.append(QStringLiteral(" no\n"));
    support.append(QStringLiteral("Number of Screens: %1\n\n").arg(screens()->count()));
    for (int i=0; i<screens()->count(); ++i) {
        const QRect geo = screens()->geometry(i);
        support.append(QStringLiteral("Screen %1:\n").arg(i));
        support.append(QStringLiteral("---------\n"));
        support.append(QStringLiteral("Name: %1\n").arg(screens()->name(i)));
        support.append(QStringLiteral("Geometry: %1,%2,%3x%4\n")
                              .arg(geo.x())
                              .arg(geo.y())
                              .arg(geo.width())
                              .arg(geo.height()));
        support.append(QStringLiteral("Scale: %1\n").arg(screens()->scale(i)));
        support.append(QStringLiteral("Refresh Rate: %1\n\n").arg(screens()->refreshRate(i)));
    }
    support.append(QStringLiteral("\nCompositing\n"));
    support.append(QStringLiteral(  "===========\n"));
    if (effects) {
        support.append(QStringLiteral("Compositing is active\n"));
        switch (effects->compositingType()) {
        case OpenGL2Compositing:
        case OpenGLCompositing: {
            GLPlatform *platform = GLPlatform::instance();
            if (platform->isGLES()) {
                support.append(QStringLiteral("Compositing Type: OpenGL ES 2.0\n"));
            } else {
                support.append(QStringLiteral("Compositing Type: OpenGL\n"));
            }
            support.append(QStringLiteral("OpenGL vendor string: ") +   QString::fromUtf8(platform->glVendorString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL renderer string: ") + QString::fromUtf8(platform->glRendererString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL version string: ") +  QString::fromUtf8(platform->glVersionString()) + QStringLiteral("\n"));
            support.append(QStringLiteral("OpenGL platform interface: "));
            switch (platform->platformInterface()) {
            case GlxPlatformInterface:
                support.append(QStringLiteral("GLX"));
                break;
            case EglPlatformInterface:
                support.append(QStringLiteral("EGL"));
                break;
            default:
                support.append(QStringLiteral("UNKNOWN"));
            }
            support.append(QStringLiteral("\n"));

            if (platform->supports(LimitedGLSL) || platform->supports(GLSL))
                support.append(QStringLiteral("OpenGL shading language version string: ") + QString::fromUtf8(platform->glShadingLanguageVersionString()) + QStringLiteral("\n"));

            support.append(QStringLiteral("Driver: ") + GLPlatform::driverToString(platform->driver()) + QStringLiteral("\n"));
            if (!platform->isMesaDriver())
                support.append(QStringLiteral("Driver version: ") + GLPlatform::versionToString(platform->driverVersion()) + QStringLiteral("\n"));

            support.append(QStringLiteral("GPU class: ") + GLPlatform::chipClassToString(platform->chipClass()) + QStringLiteral("\n"));

            support.append(QStringLiteral("OpenGL version: ") + GLPlatform::versionToString(platform->glVersion()) + QStringLiteral("\n"));

            if (platform->supports(LimitedGLSL) || platform->supports(GLSL))
                support.append(QStringLiteral("GLSL version: ") + GLPlatform::versionToString(platform->glslVersion()) + QStringLiteral("\n"));

            if (platform->isMesaDriver())
                support.append(QStringLiteral("Mesa version: ") + GLPlatform::versionToString(platform->mesaVersion()) + QStringLiteral("\n"));
            if (platform->serverVersion() > 0)
                support.append(QStringLiteral("X server version: ") + GLPlatform::versionToString(platform->serverVersion()) + QStringLiteral("\n"));
            if (platform->kernelVersion() > 0)
                support.append(QStringLiteral("Linux kernel version: ") + GLPlatform::versionToString(platform->kernelVersion()) + QStringLiteral("\n"));

            support.append(QStringLiteral("Direct rendering: "));
            support.append(QStringLiteral("Requires strict binding: "));
            if (!platform->isLooseBinding()) {
                support.append(QStringLiteral("yes\n"));
            } else {
                support.append(QStringLiteral("no\n"));
            }
            support.append(QStringLiteral("GLSL shaders: "));
            if (platform->supports(GLSL)) {
                if (platform->supports(LimitedGLSL)) {
                    support.append(QStringLiteral(" limited\n"));
                } else {
                    support.append(QStringLiteral(" yes\n"));
                }
            } else {
                support.append(QStringLiteral(" no\n"));
            }
            support.append(QStringLiteral("Texture NPOT support: "));
            if (platform->supports(TextureNPOT)) {
                if (platform->supports(LimitedNPOT)) {
                    support.append(QStringLiteral(" limited\n"));
                } else {
                    support.append(QStringLiteral(" yes\n"));
                }
            } else {
                support.append(QStringLiteral(" no\n"));
            }
            support.append(QStringLiteral("Virtual Machine: "));
            if (platform->isVirtualMachine()) {
                support.append(QStringLiteral(" yes\n"));
            } else {
                support.append(QStringLiteral(" no\n"));
            }

            support.append(QStringLiteral("OpenGL 2 Shaders are used\n"));
            break;
        }
        case XRenderCompositing:
            support.append(QStringLiteral("Compositing Type: XRender\n"));
            break;
        case QPainterCompositing:
            support.append("Compositing Type: QPainter\n");
            break;
        case NoCompositing:
        default:
            support.append(QStringLiteral("Something is really broken, neither OpenGL nor XRender is used"));
        }
        support.append(QStringLiteral("\nLoaded Effects:\n"));
        support.append(QStringLiteral(  "---------------\n"));
        foreach (const QString &effect, static_cast<EffectsHandlerImpl*>(effects)->loadedEffects()) {
            support.append(effect + QStringLiteral("\n"));
        }
        support.append(QStringLiteral("\nCurrently Active Effects:\n"));
        support.append(QStringLiteral(  "-------------------------\n"));
        foreach (const QString &effect, static_cast<EffectsHandlerImpl*>(effects)->activeEffects()) {
            support.append(effect + QStringLiteral("\n"));
        }
        support.append(QStringLiteral("\nEffect Settings:\n"));
        support.append(QStringLiteral(  "----------------\n"));
        foreach (const QString &effect, static_cast<EffectsHandlerImpl*>(effects)->loadedEffects()) {
            support.append(static_cast<EffectsHandlerImpl*>(effects)->supportInformation(effect));
            support.append(QStringLiteral("\n"));
        }
    } else {
        support.append(QStringLiteral("Compositing is not active\n"));
    }
    return support;
}

Toplevel* Workspace::findAbstractClient(std::function<bool (const Toplevel*)> func) const
{
    if (auto ret = win::find_in_list(m_allClients, func)) {
        return ret;
    }
    return nullptr;
}

Toplevel* Workspace::findUnmanaged(xcb_window_t w) const
{
    return findToplevel([w](Toplevel const* toplevel) {
        return !toplevel->control && toplevel->xcb_window() == w;
    });
}

win::x11::window* Workspace::findClient(win::x11::predicate_match predicate, xcb_window_t w) const
{
    switch (predicate) {
    case win::x11::predicate_match::window:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_window() == w;
        }));
    case win::x11::predicate_match::wrapper_id:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_windows.wrapper == w;
        }));
    case win::x11::predicate_match::frame_id:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_windows.outer == w;
        }));
    case win::x11::predicate_match::input_id:
        return qobject_cast<win::x11::window*>(findAbstractClient([w](Toplevel const* c) {
            auto x11_client = qobject_cast<win::x11::window const*>(c);
            return x11_client && x11_client->xcb_windows.input == w;
        }));
    }
    return nullptr;
}

Toplevel* Workspace::findToplevel(std::function<bool (const Toplevel*)> func) const
{
    auto const it = std::find_if(m_windows.cbegin(), m_windows.cend(),
                                 [&func](auto const& win) { return !win->remnant() && func(win); });
    return it != m_windows.cend() ? *it : nullptr;
}

void Workspace::forEachToplevel(std::function<void (Toplevel*)> func)
{
    std::for_each(m_windows.cbegin(), m_windows.cend(), func);
}

bool Workspace::hasClient(Toplevel const* window)
{
    if (auto cc = dynamic_cast<win::x11::window const*>(window)) {
        return hasClient(cc);
    } else {
        return findAbstractClient([window](Toplevel const* test) {
            return test == window;
        }) != nullptr;
    }
    return false;
}

void Workspace::forEachAbstractClient(std::function< void (Toplevel*) > func)
{
    std::for_each(m_allClients.cbegin(), m_allClients.cend(), func);
}

Toplevel* Workspace::findInternal(QWindow *w) const
{
    if (!w) {
        return nullptr;
    }
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        return findUnmanaged(w->winId());
    }
    for (auto client : m_allClients) {
        if (auto internal = qobject_cast<win::InternalClient*>(client)) {
            if (internal->internalWindow() == w) {
                return internal;
            }
        }
    }
    return nullptr;
}

bool Workspace::compositing() const
{
    return m_compositor && m_compositor->scene();
}

void Workspace::setWasUserInteraction()
{
    if (was_user_interaction) {
        return;
    }
    was_user_interaction = true;
    // might be called from within the filter, so delay till we now the filter returned
    QTimer::singleShot(0, this,
        [this] {
            m_wasUserInteractionFilter.reset();
        }
    );
}

void Workspace::updateTabbox()
{
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox *tabBox = TabBox::TabBox::self();
    if (tabBox->isDisplayed()) {
        tabBox->reset(true);
    }
#endif
}

void Workspace::addInternalClient(win::InternalClient *client)
{
    m_windows.push_back(client);
    m_allClients.push_back(client);

    setupClientConnections(client);
    win::update_layer(client);

    if (client->placeable()) {
        auto const area = clientArea(PlacementArea, screens()->current(), client->desktop());
        win::place(client, area);
    }

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);
    updateClientArea();

    emit internalClientAdded(client);
}

void Workspace::removeInternalClient(win::InternalClient *client)
{
    remove_all(m_allClients, client);
    remove_all(m_windows, client);

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);
    updateClientArea();

    emit internalClientRemoved(client);
}

void Workspace::remove_window(Toplevel* window)
{
    remove_all(m_windows, window);
    remove_all(stacking_order->pre_stack, window);
    remove_all(stacking_order->win_stack, window);

    x_stacking_tree->mark_as_dirty();
    stacking_order->update(true);
}

win::x11::Group* Workspace::findGroup(xcb_window_t leader) const
{
    Q_ASSERT(leader != XCB_WINDOW_NONE);
    for (auto it = groups.cbegin();
            it != groups.cend();
            ++it)
        if ((*it)->leader() == leader)
            return *it;
    return nullptr;
}

void Workspace::updateMinimizedOfTransients(Toplevel* c)
{
    // if mainwindow is minimized or shaded, minimize transients too
    auto const transients = c->transient()->children;

    if (c->control->minimized()) {
        for (auto it = transients.cbegin();
                it != transients.cend();
                ++it) {
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
        if (c->transient()->modal()) { // if a modal dialog is minimized, minimize its mainwindow too
            for (auto c2 : c->transient()->leads()) {
                win::set_minimized(c2, true);
            }
        }
    } else {
        // else unmiminize the transients
        for (auto it = transients.cbegin();
                it != transients.cend();
                ++it) {
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
void Workspace::updateOnAllDesktopsOfTransients(Toplevel* window)
{
    auto const transients = window->transient()->children;
    for (auto const& transient : transients) {
        if (transient->isOnAllDesktops() != window->isOnAllDesktops()) {
            win::set_on_all_desktops(transient, window->isOnAllDesktops());
        }
    }
}

// A new window has been mapped. Check if it's not a mainwindow for some already existing transient window.
void Workspace::checkTransients(Toplevel* window)
{
    std::for_each(m_windows.cbegin(), m_windows.cend(),
        [&window](auto const& client) {client->checkTransient(window);});
}

/**
 * Resizes the workspace after an XRANDR screen size change
 */
void Workspace::desktopResized()
{
    QRect geom = screens()->geometry();
    if (win::x11::rootInfo()) {
        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        win::x11::rootInfo()->setDesktopGeometry(desktop_geometry);
    }

    updateClientArea();
    saveOldScreenSizes(); // after updateClientArea(), so that one still uses the previous one

    // TODO: emit a signal instead and remove the deep function calls into edges and effects
    ScreenEdges::self()->recreateEdges();

    if (effects) {
        static_cast<EffectsHandlerImpl*>(effects)->desktopResized(geom.size());
    }
}

void Workspace::saveOldScreenSizes()
{
    olddisplaysize = screens()->displaySize();
    oldscreensizes.clear();
    for( int i = 0;
         i < screens()->count();
         ++i )
        oldscreensizes.push_back( screens()->geometry( i ));
}

/**
 * Updates the current client areas according to the current clients.
 *
 * If the area changes or force is @c true, the new areas are propagated to the world.
 *
 * The client area is the area that is available for clients (that
 * which is not taken by windows like panels, the top-of-screen menu
 * etc).
 *
 * @see clientArea()
 */
void Workspace::updateClientArea(bool force)
{
    const Screens *s = Screens::self();
    int nscreens = s->count();
    const int numberOfDesktops = VirtualDesktopManager::self()->count();
    std::vector<QRect> new_wareas(numberOfDesktops + 1);
    std::vector<StrutRects> new_rmoveareas(numberOfDesktops + 1);
    std::vector<std::vector<QRect>> new_sareas(numberOfDesktops + 1);
    QVector< QRect > screens(nscreens);
    QRect desktopArea;
    for (int i = 0; i < nscreens; i++) {
        desktopArea |= s->geometry(i);
    }
    for (int iS = 0;
            iS < nscreens;
            iS ++) {
        screens [iS] = s->geometry(iS);
    }
    for (int i = 1;
            i <= numberOfDesktops;
            ++i) {
        new_wareas[ i ] = desktopArea;
        new_sareas[ i ].resize(nscreens);
        for (int iS = 0;
                iS < nscreens;
                iS ++)
            new_sareas[ i ][ iS ] = screens[ iS ];
    }
    for (auto const& client : m_allClients) {

        // TODO(romangg): Merge this with Wayland clients below.
        auto x11_client = qobject_cast<win::x11::window*>(client);
        if (!x11_client) {
            continue;
        }
        if (!x11_client->hasStrut()) {
            continue;
        }

        auto r = win::x11::adjusted_client_area(x11_client, desktopArea, desktopArea);
        // sanity check that a strut doesn't exclude a complete screen geometry
        // this is a violation to EWMH, as KWin just ignores the strut
        for (int i = 0; i < Screens::self()->count(); i++) {
            if (!r.intersects(Screens::self()->geometry(i))) {
                qCDebug(KWIN_CORE) << "Adjusted client area would exclude a complete screen, ignore";
                r = desktopArea;
                break;
            }
        }
        StrutRects strutRegion = win::x11::strut_rects(x11_client);
        const QRect clientsScreenRect = KWin::screens()->geometry(x11_client->screen());
        for (auto strut = strutRegion.begin(); strut != strutRegion.end(); strut++) {
            *strut = StrutRect((*strut).intersected(clientsScreenRect), (*strut).area());
        }

        // Ignore offscreen xinerama struts. These interfere with the larger monitors on the setup
        // and should be ignored so that applications that use the work area to work out where
        // windows can go can use the entire visible area of the larger monitors.
        // This goes against the EWMH description of the work area but it is a toss up between
        // having unusable sections of the screen (Which can be quite large with newer monitors)
        // or having some content appear offscreen (Relatively rare compared to other).
        bool hasOffscreenXineramaStrut = win::x11::has_offscreen_xinerama_strut(x11_client);

        if (x11_client->isOnAllDesktops()) {
            for (int i = 1;
                    i <= numberOfDesktops;
                    ++i) {
                if (!hasOffscreenXineramaStrut)
                    new_wareas[ i ] = new_wareas[ i ].intersected(r);
                new_rmoveareas[ i ] += strutRegion;
                for (int iS = 0;
                        iS < nscreens;
                        iS ++) {
                    const auto geo = new_sareas[ i ][ iS ].intersected(
                                                win::x11::adjusted_client_area(x11_client, desktopArea, screens[ iS ]));
                    // ignore the geometry if it results in the screen getting removed completely
                    if (!geo.isEmpty()) {
                        new_sareas[ i ][ iS ] = geo;
                    }
                }
            }
        } else {
            if (!hasOffscreenXineramaStrut)
                new_wareas[x11_client->desktop()] = new_wareas[x11_client->desktop()].intersected(r);
            new_rmoveareas[x11_client->desktop()] += strutRegion;
            for (int iS = 0;
                    iS < nscreens;
                    iS ++) {
//                            qDebug() << "adjusting new_sarea: " << screens[ iS ];
                const auto geo = new_sareas[x11_client->desktop()][ iS ].intersected(
                      win::x11::adjusted_client_area(x11_client, desktopArea, screens[ iS ]));
                // ignore the geometry if it results in the screen getting removed completely
                if (!geo.isEmpty()) {
                    new_sareas[x11_client->desktop()][ iS ] = geo;
                }
            }
        }
    }
    if (waylandServer()) {
        auto updateStrutsForWaylandClient = [&] (win::wayland::window* c) {
            // assuming that only docks have "struts" and that all docks have a strut
            if (!c->hasStrut()) {
                return;
            }
            auto margins = [c] (const QRect &geometry) {
                QMargins margins;
                if (!geometry.intersects(c->frameGeometry())) {
                    return margins;
                }
                // figure out which areas of the overall screen setup it borders
                const bool left = c->frameGeometry().left() == geometry.left();
                const bool right = c->frameGeometry().right() == geometry.right();
                const bool top = c->frameGeometry().top() == geometry.top();
                const bool bottom = c->frameGeometry().bottom() == geometry.bottom();
                const bool horizontal = c->frameGeometry().width() >= c->frameGeometry().height();
                if (left && ((!top && !bottom) || !horizontal)) {
                    margins.setLeft(c->frameGeometry().width());
                }
                if (right && ((!top && !bottom) || !horizontal)) {
                    margins.setRight(c->frameGeometry().width());
                }
                if (top && ((!left && !right) || horizontal)) {
                    margins.setTop(c->frameGeometry().height());
                }
                if (bottom && ((!left && !right) || horizontal)) {
                    margins.setBottom(c->frameGeometry().height());
                }
                return margins;
            };
            auto marginsToStrutArea = [] (const QMargins &margins) {
                if (margins.left() != 0) {
                    return StrutAreaLeft;
                }
                if (margins.right() != 0) {
                    return StrutAreaRight;
                }
                if (margins.top() != 0) {
                    return StrutAreaTop;
                }
                if (margins.bottom() != 0) {
                    return StrutAreaBottom;
                }
                return StrutAreaInvalid;
            };
            const auto strut = margins(KWin::screens()->geometry(c->screen()));
            const StrutRects strutRegion = StrutRects{StrutRect(c->frameGeometry(), marginsToStrutArea(strut))};
            QRect r = desktopArea - margins(KWin::screens()->geometry());
            if (c->isOnAllDesktops()) {
                for (int i = 1; i <= numberOfDesktops; ++i) {
                    new_wareas[ i ] = new_wareas[ i ].intersected(r);
                    for (int iS = 0; iS < nscreens; ++iS) {
                        new_sareas[ i ][ iS ] = new_sareas[ i ][ iS ].intersected(screens[iS] - margins(screens[iS]));
                    }
                    new_rmoveareas[ i ] += strutRegion;
                }
            } else {
                new_wareas[c->desktop()] = new_wareas[c->desktop()].intersected(r);
                for (int iS = 0; iS < nscreens; iS++) {
                    new_sareas[c->desktop()][ iS ] = new_sareas[c->desktop()][ iS ].intersected(screens[iS] - margins(screens[iS]));
                }
                new_rmoveareas[ c->desktop() ] += strutRegion;
            }
        };
        const auto wayland_windows = waylandServer()->windows;
        for (auto win : wayland_windows) {
            updateStrutsForWaylandClient(win);
        }
    }
#if 0
    for (int i = 1;
            i <= numberOfDesktops();
            ++i) {
        for (int iS = 0;
                iS < nscreens;
                iS ++)
            qCDebug(KWIN_CORE) << "new_sarea: " << new_sareas[ i ][ iS ];
    }
#endif

    bool changed = force || screenarea.empty();
    for (int i = 1;
            !changed && i <= numberOfDesktops;
            ++i) {
        changed |= workarea[i] != new_wareas[i];
        changed |= restrictedmovearea[i] != new_rmoveareas[i];
        changed |= screenarea[i].size() != new_sareas[i].size();
        for (int iS = 0; !changed && iS < nscreens; iS++) {
            changed |= new_sareas[i][iS] != screenarea[i][iS];
        }
    }

    if (changed) {
        workarea = new_wareas;
        oldrestrictedmovearea = restrictedmovearea;
        restrictedmovearea = new_rmoveareas;
        screenarea = new_sareas;
        if (win::x11::rootInfo()) {
            NETRect r;
            for (int i = 1; i <= numberOfDesktops; i++) {
                r.pos.x = workarea[ i ].x();
                r.pos.y = workarea[ i ].y();
                r.size.width = workarea[ i ].width();
                r.size.height = workarea[ i ].height();
                win::x11::rootInfo()->setWorkArea(i, r);
            }
        }

        for (auto it = m_allClients.cbegin();
                it != m_allClients.cend();
                ++it)
            win::check_workspace_position(*it);

        oldrestrictedmovearea.clear(); // reset, no longer valid or needed
    }
}

void Workspace::updateClientArea()
{
    updateClientArea(false);
}


/**
 * Returns the area available for clients. This is the desktop
 * geometry minus windows on the dock. Placement algorithms should
 * refer to this rather than Screens::geometry.
 */
QRect Workspace::clientArea(clientAreaOption opt, int screen, int desktop) const
{
    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0)
        desktop = VirtualDesktopManager::self()->current();
    if (screen == -1) {
        screen = screens()->current();
    }
    const QSize displaySize = screens()->displaySize();

    QRect sarea, warea;
    sarea = (!screenarea.empty()
             // screens may be missing during KWin initialization or screen config changes
            && screen < static_cast<int>(screenarea[ desktop ].size()))
            ? screenarea[ desktop ][ screen ]
            : screens()->geometry(screen);
    warea = workarea[ desktop ].isNull()
            ? QRect(0, 0, displaySize.width(), displaySize.height())
            : workarea[ desktop ];

    switch(opt) {
    case MaximizeArea:
    case PlacementArea:
            return sarea;
    case MaximizeFullArea:
    case FullScreenArea:
    case MovementArea:
    case ScreenArea:
        return screens()->geometry(screen);
    case WorkArea:
        return warea;
    case FullArea:
        return QRect(0, 0, displaySize.width(), displaySize.height());
    }
    abort();
}


QRect Workspace::clientArea(clientAreaOption opt, const QPoint& p, int desktop) const
{
    return clientArea(opt, screens()->number(p), desktop);
}

QRect Workspace::clientArea(clientAreaOption opt, Toplevel const* window) const
{
    return clientArea(opt, win::pending_frame_geometry(window).center(), window->desktop());
}

static QRegion strutsToRegion(int desktop, StrutAreas areas, std::vector<StrutRects> const& struts)
{
    if (desktop == NETWinInfo::OnAllDesktops || desktop == 0)
        desktop = VirtualDesktopManager::self()->current();
    QRegion region;
    const StrutRects &rects = struts[desktop];
    for (const StrutRect &rect : rects) {
        if (areas & rect.area()) {
            region += rect;
        }
    }
    return region;
}

QRegion Workspace::restrictedMoveArea(int desktop, StrutAreas areas) const
{
    return strutsToRegion(desktop, areas, restrictedmovearea);
}

bool Workspace::inUpdateClientArea() const
{
    return !oldrestrictedmovearea.empty();
}

QRegion Workspace::previousRestrictedMoveArea(int desktop, StrutAreas areas) const
{
    return strutsToRegion(desktop, areas, oldrestrictedmovearea);
}

std::vector<QRect> Workspace::previousScreenSizes() const
{
    return oldscreensizes;
}

int Workspace::oldDisplayWidth() const
{
    return olddisplaysize.width();
}

int Workspace::oldDisplayHeight() const
{
    return olddisplaysize.height();
}

/**
 * Client \a c is moved around to position \a pos. This gives the
 * workspace the opportunity to interveniate and to implement
 * snap-to-windows functionality.
 *
 * The parameter \a snapAdjust is a multiplier used to calculate the
 * effective snap zones. When 1.0, it means that the snap zones will be
 * used without change.
 */
QPoint Workspace::adjustClientPosition(Toplevel* window, QPoint pos, bool unrestricted, double snapAdjust)
{
    QSize borderSnapZone(options->borderSnapZone(), options->borderSnapZone());
    QRect maxRect;
    auto guideMaximized = win::maximize_mode::restore;
    if (window->maximizeMode() != win::maximize_mode::restore) {
        maxRect = clientArea(MaximizeArea, pos + QRect(QPoint(), window->size()).center(),
                             window->desktop());
        QRect geo = window->frameGeometry();
        if (win::flags(window->maximizeMode() & win::maximize_mode::horizontal)
                && (geo.x() == maxRect.left() || geo.right() == maxRect.right())) {
            guideMaximized |= win::maximize_mode::horizontal;
            borderSnapZone.setWidth(qMax(borderSnapZone.width() + 2, maxRect.width() / 16));
        }
        if (win::flags(window->maximizeMode() & win::maximize_mode::vertical)
                && (geo.y() == maxRect.top() || geo.bottom() == maxRect.bottom())) {
            guideMaximized |= win::maximize_mode::vertical;
            borderSnapZone.setHeight(qMax(borderSnapZone.height() + 2, maxRect.height() / 16));
        }
    }

    if (options->windowSnapZone() || !borderSnapZone.isNull() || options->centerSnapZone()) {

        const bool sOWO = options->isSnapOnlyWhenOverlapping();
        const int screen = screens()->number(pos + QRect(QPoint(), window->size()).center());

        if (maxRect.isNull()) {
            maxRect = clientArea(MovementArea, screen, window->desktop());
        }

        const int xmin = maxRect.left();
        const int xmax = maxRect.right() + 1;             //desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom() + 1;

        const int cx(pos.x());
        const int cy(pos.y());
        const int cw(window->size().width());
        const int ch(window->size().height());
        const int rx(cx + cw);
        const int ry(cy + ch);               //these don't change

        int nx(cx), ny(cy);                         //buffers
        int deltaX(xmax);
        int deltaY(ymax);   //minimum distance to other clients

        int lx, ly, lrx, lry; //coords and size for the comparison client, l

        // border snap
        const int snapX = borderSnapZone.width() * snapAdjust; //snap trigger
        const int snapY = borderSnapZone.height() * snapAdjust;
        if (snapX || snapY) {
            auto geo = window->frameGeometry();
            auto frameMargins = win::frame_margins(window);

            // snap to titlebar / snap to window borders on inner screen edges
            if (frameMargins.left() && (win::flags(window->maximizeMode() & win::maximize_mode::horizontal) ||
                                        screens()->intersecting(geo.translated(maxRect.x() - (frameMargins.left() + geo.x()), 0)) > 1)) {
                frameMargins.setLeft(0);
            }
            if (frameMargins.right() && (win::flags(window->maximizeMode() & win::maximize_mode::horizontal) ||
                                         screens()->intersecting(geo.translated(maxRect.right() + frameMargins.right() - geo.right(), 0)) > 1)) {
                frameMargins.setRight(0);
            }
            if (frameMargins.top()) {
                frameMargins.setTop(0);
            }
            if (frameMargins.bottom() && (win::flags(window->maximizeMode() & win::maximize_mode::vertical) ||
                                          screens()->intersecting(geo.translated(0, maxRect.bottom() + frameMargins.bottom() - geo.bottom())) > 1)) {
                frameMargins.setBottom(0);
            }
            if ((sOWO ? (cx < xmin) : true) && (qAbs(xmin - cx) < snapX)) {
                deltaX = xmin - cx;
                nx = xmin - frameMargins.left();
            }
            if ((sOWO ? (rx > xmax) : true) && (qAbs(rx - xmax) < snapX) && (qAbs(xmax - rx) < deltaX)) {
                deltaX = rx - xmax;
                nx = xmax - cw + frameMargins.right();
            }

            if ((sOWO ? (cy < ymin) : true) && (qAbs(ymin - cy) < snapY)) {
                deltaY = ymin - cy;
                ny = ymin - frameMargins.top();
            }
            if ((sOWO ? (ry > ymax) : true) && (qAbs(ry - ymax) < snapY) && (qAbs(ymax - ry) < deltaY)) {
                deltaY = ry - ymax;
                ny = ymax - ch + frameMargins.bottom();
            }
        }

        // windows snap
        int snap = options->windowSnapZone() * snapAdjust;
        if (snap) {
            for (auto l = m_allClients.cbegin(); l != m_allClients.cend(); ++l) {
                if ((*l) == window)
                    continue;
                if ((*l)->control->minimized())
                    continue; // is minimized
                if (!(*l)->isShown())
                    continue;
                if (!((*l)->isOnDesktop(window->desktop()) || window->isOnDesktop((*l)->desktop())))
                    continue; // wrong virtual desktop
                if (!(*l)->isOnCurrentActivity())
                    continue; // wrong activity
                if (win::is_desktop(*l) || win::is_splash(*l))
                    continue;

                lx = (*l)->pos().x();
                ly = (*l)->pos().y();
                lrx = lx + (*l)->size().width();
                lry = ly + (*l)->size().height();

                if (!win::flags(guideMaximized & win::maximize_mode::horizontal) &&
                    (((cy <= lry) && (cy  >= ly)) || ((ry >= ly) && (ry  <= lry)) || ((cy <= ly) && (ry >= lry)))) {
                    if ((sOWO ? (cx < lrx) : true) && (qAbs(lrx - cx) < snap) && (qAbs(lrx - cx) < deltaX)) {
                        deltaX = qAbs(lrx - cx);
                        nx = lrx;
                    }
                    if ((sOWO ? (rx > lx) : true) && (qAbs(rx - lx) < snap) && (qAbs(rx - lx) < deltaX)) {
                        deltaX = qAbs(rx - lx);
                        nx = lx - cw;
                    }
                }

                if (!win::flags(guideMaximized & win::maximize_mode::vertical) &&
                    (((cx <= lrx) && (cx  >= lx)) || ((rx >= lx) && (rx  <= lrx)) || ((cx <= lx) && (rx >= lrx)))) {
                    if ((sOWO ? (cy < lry) : true) && (qAbs(lry - cy) < snap) && (qAbs(lry - cy) < deltaY)) {
                        deltaY = qAbs(lry - cy);
                        ny = lry;
                    }
                    //if ( (qAbs( ry-ly ) < snap) && (qAbs( ry - ly ) < deltaY ))
                    if ((sOWO ? (ry > ly) : true) && (qAbs(ry - ly) < snap) && (qAbs(ry - ly) < deltaY)) {
                        deltaY = qAbs(ry - ly);
                        ny = ly - ch;
                    }
                }

                // Corner snapping
                if (!win::flags(guideMaximized & win::maximize_mode::vertical) && (nx == lrx || nx + cw == lx)) {
                    if ((sOWO ? (ry > lry) : true) && (qAbs(lry - ry) < snap) && (qAbs(lry - ry) < deltaY)) {
                        deltaY = qAbs(lry - ry);
                        ny = lry - ch;
                    }
                    if ((sOWO ? (cy < ly) : true) && (qAbs(cy - ly) < snap) && (qAbs(cy - ly) < deltaY)) {
                        deltaY = qAbs(cy - ly);
                        ny = ly;
                    }
                }
                if (!win::flags(guideMaximized & win::maximize_mode::horizontal) && (ny == lry || ny + ch == ly)) {
                    if ((sOWO ? (rx > lrx) : true) && (qAbs(lrx - rx) < snap) && (qAbs(lrx - rx) < deltaX)) {
                        deltaX = qAbs(lrx - rx);
                        nx = lrx - cw;
                    }
                    if ((sOWO ? (cx < lx) : true) && (qAbs(cx - lx) < snap) && (qAbs(cx - lx) < deltaX)) {
                        deltaX = qAbs(cx - lx);
                        nx = lx;
                    }
                }
            }
        }

        // center snap
        snap = options->centerSnapZone() * snapAdjust; //snap trigger
        if (snap) {
            int diffX = qAbs((xmin + xmax) / 2 - (cx + cw / 2));
            int diffY = qAbs((ymin + ymax) / 2 - (cy + ch / 2));
            if (diffX < snap && diffY < snap && diffX < deltaX && diffY < deltaY) {
                // Snap to center of screen
                nx = (xmin + xmax) / 2 - cw / 2;
                ny = (ymin + ymax) / 2 - ch / 2;
            } else if (options->borderSnapZone()) {
                // Enhance border snap
                if ((nx == xmin || nx == xmax - cw) && diffY < snap && diffY < deltaY) {
                    // Snap to vertical center on screen edge
                    ny = (ymin + ymax) / 2 - ch / 2;
                } else if (((unrestricted ? ny == ymin : ny <= ymin) || ny == ymax - ch) &&
                          diffX < snap && diffX < deltaX) {
                    // Snap to horizontal center on screen edge
                    nx = (xmin + xmax) / 2 - cw / 2;
                }
            }
        }

        pos = QPoint(nx, ny);
    }
    return pos;
}

QRect Workspace::adjustClientSize(Toplevel* window, QRect moveResizeGeom, win::position mode)
{
    //adapted from adjustClientPosition on 29May2004
    //this function is called when resizing a window and will modify
    //the new dimensions to snap to other windows/borders if appropriate
    if (options->windowSnapZone() || options->borderSnapZone()) {  // || options->centerSnapZone )
        const bool sOWO = options->isSnapOnlyWhenOverlapping();

        auto const maxRect = clientArea(MovementArea, QRect(QPoint(0, 0), window->size()).center(),
                                        window->desktop());
        const int xmin = maxRect.left();
        const int xmax = maxRect.right();               //desk size
        const int ymin = maxRect.top();
        const int ymax = maxRect.bottom();

        const int cx(moveResizeGeom.left());
        const int cy(moveResizeGeom.top());
        const int rx(moveResizeGeom.right());
        const int ry(moveResizeGeom.bottom());

        int newcx(cx), newcy(cy);                         //buffers
        int newrx(rx), newry(ry);
        int deltaX(xmax);
        int deltaY(ymax);   //minimum distance to other clients

        int lx, ly, lrx, lry; //coords and size for the comparison client, l

        // border snap
        int snap = options->borderSnapZone(); //snap trigger
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);

#define SNAP_BORDER_TOP \
    if ((sOWO?(newcy<ymin):true) && (qAbs(ymin-newcy)<deltaY)) \
    { \
        deltaY = qAbs(ymin-newcy); \
        newcy = ymin; \
    }

#define SNAP_BORDER_BOTTOM \
    if ((sOWO?(newry>ymax):true) && (qAbs(ymax-newry)<deltaY)) \
    { \
        deltaY = qAbs(ymax-newcy); \
        newry = ymax; \
    }

#define SNAP_BORDER_LEFT \
    if ((sOWO?(newcx<xmin):true) && (qAbs(xmin-newcx)<deltaX)) \
    { \
        deltaX = qAbs(xmin-newcx); \
        newcx = xmin; \
    }

#define SNAP_BORDER_RIGHT \
    if ((sOWO?(newrx>xmax):true) && (qAbs(xmax-newrx)<deltaX)) \
    { \
        deltaX = qAbs(xmax-newrx); \
        newrx = xmax; \
    }
            switch(mode) {
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
        snap = options->windowSnapZone();
        if (snap) {
            deltaX = int(snap);
            deltaY = int(snap);
            for (auto l = m_allClients.cbegin(); l != m_allClients.cend(); ++l) {
                if ((*l)->isOnDesktop(VirtualDesktopManager::self()->current()) &&
                        !(*l)->control->minimized()
                        && (*l) != window) {
                    lx = (*l)->pos().x() - 1;
                    ly = (*l)->pos().y() - 1;
                    lrx = (*l)->pos().x() + (*l)->size().width();
                    lry = (*l)->pos().y() + (*l)->size().height();

#define WITHIN_HEIGHT ((( newcy <= lry ) && ( newcy  >= ly  ))  || \
                       (( newry >= ly  ) && ( newry  <= lry ))  || \
                       (( newcy <= ly  ) && ( newry >= lry  )) )

#define WITHIN_WIDTH  ( (( cx <= lrx ) && ( cx  >= lx  ))  || \
                        (( rx >= lx  ) && ( rx  <= lrx ))  || \
                        (( cx <= lx  ) && ( rx >= lrx  )) )

#define SNAP_WINDOW_TOP  if ( (sOWO?(newcy<lry):true) \
                              && WITHIN_WIDTH  \
                              && (qAbs( lry - newcy ) < deltaY) ) {  \
    deltaY = qAbs( lry - newcy ); \
    newcy=lry; \
}

#define SNAP_WINDOW_BOTTOM  if ( (sOWO?(newry>ly):true)  \
                                 && WITHIN_WIDTH  \
                                 && (qAbs( ly - newry ) < deltaY) ) {  \
    deltaY = qAbs( ly - newry );  \
    newry=ly;  \
}

#define SNAP_WINDOW_LEFT  if ( (sOWO?(newcx<lrx):true)  \
                               && WITHIN_HEIGHT  \
                               && (qAbs( lrx - newcx ) < deltaX)) {  \
    deltaX = qAbs( lrx - newcx );  \
    newcx=lrx;  \
}

#define SNAP_WINDOW_RIGHT  if ( (sOWO?(newrx>lx):true)  \
                                && WITHIN_HEIGHT  \
                                && (qAbs( lx - newrx ) < deltaX))  \
{  \
    deltaX = qAbs( lx - newrx );  \
    newrx=lx;  \
}

#define SNAP_WINDOW_C_TOP  if ( (sOWO?(newcy<ly):true)  \
                                && (newcx == lrx || newrx == lx)  \
                                && qAbs(ly-newcy) < deltaY ) {  \
    deltaY = qAbs( ly - newcy + 1 ); \
    newcy = ly + 1; \
}

#define SNAP_WINDOW_C_BOTTOM  if ( (sOWO?(newry>lry):true)  \
                                   && (newcx == lrx || newrx == lx)  \
                                   && qAbs(lry-newry) < deltaY ) {  \
    deltaY = qAbs( lry - newry - 1 ); \
    newry = lry - 1; \
}

#define SNAP_WINDOW_C_LEFT  if ( (sOWO?(newcx<lx):true)  \
                                 && (newcy == lry || newry == ly)  \
                                 && qAbs(lx-newcx) < deltaX ) {  \
    deltaX = qAbs( lx - newcx + 1 ); \
    newcx = lx + 1; \
}

#define SNAP_WINDOW_C_RIGHT  if ( (sOWO?(newrx>lrx):true)  \
                                  && (newcy == lry || newry == ly)  \
                                  && qAbs(lrx-newrx) < deltaX ) {  \
    deltaX = qAbs( lrx - newrx - 1 ); \
    newrx = lrx - 1; \
}

                    switch(mode) {
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
        //snap = options->centerSnapZone;
        //if (snap)
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
void Workspace::setMoveResizeClient(Toplevel* window)
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
void Workspace::fixPositionAfterCrash(xcb_window_t w, const xcb_get_geometry_reply_t *geometry)
{
    NETWinInfo i(connection(), w, rootWindow(), NET::WMFrameExtents, NET::Properties2());
    NETStrut frame = i.frameExtents();

    if (frame.left != 0 || frame.top != 0) {
        // left and top needed due to narrowing conversations restrictions in C++11
        const uint32_t left = frame.left;
        const uint32_t top = frame.top;
        const uint32_t values[] = { geometry->x - left, geometry->y - top };
        xcb_configure_window(connection(), w, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    }
}

bool Workspace::hasClient(win::x11::window const* c)
{
    auto abstract_c = static_cast<Toplevel const*>(c);
    return findAbstractClient([abstract_c](Toplevel const* test) {
        return test == abstract_c;
    });
}

std::vector<Toplevel*> const& Workspace::windows() const
{
    return m_windows;
}

std::vector<Toplevel*> Workspace::unmanagedList() const
{
    std::vector<Toplevel*> ret;
    for (auto const& window : m_windows) {
        if (window->xcb_window() && !window->control && !window->remnant()) {
            ret.push_back(window);
        }
    }
    return ret;
}

std::vector<Toplevel*> Workspace::remnants() const
{
    std::vector<Toplevel*> ret;
    for (auto const& window : m_windows) {
        if (window->remnant()) {
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
void Workspace::slotWindowPackLeft()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    win::pack_to(active_client, packPositionLeft(active_client, pos.x(), true), pos.y());
}

void Workspace::slotWindowPackRight()
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

void Workspace::slotWindowPackUp()
{
    if (!win::can_move(active_client)) {
        return;
    }
    auto const pos = active_client->geometry_update.frame.topLeft();
    win::pack_to(active_client, pos.x(), packPositionUp(active_client, pos.y(), true));
}

void Workspace::slotWindowPackDown()
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

void Workspace::slotWindowGrowHorizontal()
{
    if (active_client) {
        win::grow_horizontal(active_client);
    }
}

void Workspace::slotWindowShrinkHorizontal()
{
    if (active_client) {
        win::shrink_horizontal(active_client);
    }
}
void Workspace::slotWindowGrowVertical()
{
    if (active_client) {
        win::grow_vertical(active_client);
    }
}

void Workspace::slotWindowShrinkVertical()
{
    if (active_client) {
        win::shrink_vertical(active_client);
    }
}

void Workspace::quickTileWindow(win::quicktiles mode)
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

        auto const is_left_or_right = mode == win::quicktiles::left
            || mode == win::quicktiles::right;
        auto const is_top_or_bottom = mode == win::quicktiles::top
            || mode == win::quicktiles::bottom;

        if ((was_left_or_right && is_top_or_bottom) || (was_top_or_bottom && is_left_or_right)) {
            mode |= m_lastTilingMode;
        }
        m_quickTileCombineTimer->stop();
    }

    win::set_quicktile_mode(active_client, mode, true);
}

int Workspace::packPositionLeft(Toplevel const* window, int oldX, bool leftEdge) const
{
    int newX = clientArea(MaximizeArea, window).left();
    if (oldX <= newX) { // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.left() - 1, window->geometry_update.frame.center().y()), window->desktop()).left();
    }

    auto const right = newX - win::frame_margins(window).left();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (screens()->intersecting(frameGeometry) < 2) {
        newX = right;
    }

    if (oldX <= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int x = leftEdge ? (*it)->geometry_update.frame.right() + 1 : (*it)->geometry_update.frame.left() - 1;
        if (x > newX && x < oldX
                && !(window->geometry_update.frame.top() > (*it)->geometry_update.frame.bottom()
                     || window->geometry_update.frame.bottom() < (*it)->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

int Workspace::packPositionRight(Toplevel const* window, int oldX, bool rightEdge) const
{
    int newX = clientArea(MaximizeArea, window).right();

    if (oldX >= newX) {
        // try another Xinerama screen
        newX = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.right() + 1, window->geometry_update.frame.center().y()), window->desktop()).right();
    }

    auto const right = newX + win::frame_margins(window).right();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (screens()->intersecting(frameGeometry) < 2) {
        newX = right;
    }

    if (oldX >= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int x = rightEdge ? (*it)->geometry_update.frame.left() - 1 : (*it)->geometry_update.frame.right() + 1;
        if (x < newX && x > oldX
                && !(window->geometry_update.frame.top() > (*it)->geometry_update.frame.bottom()
                     || window->geometry_update.frame.bottom() < (*it)->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

int Workspace::packPositionUp(Toplevel const* window, int oldY, bool topEdge) const
{
    int newY = clientArea(MaximizeArea, window).top();
    if (oldY <= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.center().x(), window->geometry_update.frame.top() - 1), window->desktop()).top();
    }

    if (oldY <= newY) {
        return oldY;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int y = topEdge ? (*it)->geometry_update.frame.bottom() + 1 : (*it)->geometry_update.frame.top() - 1;
        if (y > newY && y < oldY
                && !(window->geometry_update.frame.left() > (*it)->geometry_update.frame.right()  // they overlap in X direction
                     || window->geometry_update.frame.right() < (*it)->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

int Workspace::packPositionDown(Toplevel const* window, int oldY, bool bottomEdge) const
{
    int newY = clientArea(MaximizeArea, window).bottom();
    if (oldY >= newY) { // try another Xinerama screen
        newY = clientArea(MaximizeArea,
                          QPoint(window->geometry_update.frame.center().x(), window->geometry_update.frame.bottom() + 1), window->desktop()).bottom();
    }

    auto const bottom = newY + win::frame_margins(window).bottom();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveBottom(bottom);
    if (screens()->intersecting(frameGeometry) < 2) {
        newY = bottom;
    }

    if (oldY >= newY) {
        return oldY;
    }
    const int desktop = window->desktop() == 0 || window->isOnAllDesktops() ? VirtualDesktopManager::self()->current() : window->desktop();
    for (auto it = m_allClients.cbegin(), end = m_allClients.cend(); it != end; ++it) {
        if (win::is_irrelevant(*it, window, desktop)) {
            continue;
        }
        const int y = bottomEdge ? (*it)->geometry_update.frame.top() - 1 : (*it)->geometry_update.frame.bottom() + 1;
        if (y < newY && y > oldY
                && !(window->geometry_update.frame.left() > (*it)->geometry_update.frame.right()
                     || window->geometry_update.frame.right() < (*it)->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

#endif

} // namespace
