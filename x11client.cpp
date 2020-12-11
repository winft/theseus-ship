/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

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
#include "x11client.h"

#ifdef KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif

#include "atoms.h"
#include "client_machine.h"
#include "composite.h"
#include "cursor.h"
#include "decorations/decorationbridge.h"
#include "decorations/decoratedclient.h"
#include "effects.h"
#include "focuschain.h"
#include "geometrytip.h"
#include "group.h"
#include "netinfo.h"
#include "rules/rule_book.h"
#include "screenedge.h"
#include "screens.h"
#include "shadow.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

#include "win/controlling.h"
#include "win/geo.h"
#include "win/input.h"
#include "win/meta.h"
#include "win/remnant.h"
#include "win/rules.h"
#include "win/scene.h"
#include "win/setup.h"
#include "win/space.h"
#include "win/transient.h"

#include "win/x11/xcb.h"

#include "workspace.h"

#include <KColorScheme>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecoratedClient>
#include <KLocalizedString>
#include <KStartupInfo>
#include <KWindowSystem>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QProcess>

#include <xcb/xcb_icccm.h>

#include <csignal>
#include <unistd.h>

namespace KWin
{

class x11_transient : public win::transient
{
public:
    x11_transient(X11Client* client)
        : transient(client)
        , m_client{client}
    {
    }

    void remove_child(Toplevel* window) override
    {
        // window is transient for m_client, but m_client is going away
        // make window instead a group transient.
        transient::remove_child(window);

        if (!window->transient()->lead()) {
            if (auto x11_window = qobject_cast<X11Client*>(window)) {
                x11_window->m_transientForId = XCB_WINDOW_NONE;
                x11_window->set_transient_lead(XCB_WINDOW_NONE);
            }
        }
    }

private:
    X11Client* m_client;
};

class x11_control : public win::control
{
public:
    x11_control(X11Client* client)
        : control(client)
        , m_client{client}
    {
    }

    void set_skip_pager(bool set) override
    {
        control::set_skip_pager(set);
        m_client->info->setState(skip_pager() ? NET::SkipPager : NET::States(), NET::SkipPager);
    }

    void set_skip_switcher(bool set) override
    {
        control::set_skip_switcher(set);
        m_client->info->setState(skip_switcher() ? NET::SkipSwitcher : NET::States(),
                                 NET::SkipSwitcher);
    }

    void set_skip_taskbar(bool set) override
    {
        control::set_skip_taskbar(set);
        m_client->info->setState(skip_taskbar() ? NET::SkipTaskbar : NET::States(),
                                 NET::SkipTaskbar);
    }

    void update_mouse_grab() override
    {
        xcb_ungrab_button(connection(), XCB_BUTTON_INDEX_ANY, m_client->m_wrapper, XCB_MOD_MASK_ANY);

        if (TabBox::TabBox::self()->forcedGlobalMouseGrab()) { // see TabBox::establishTabBoxGrab()
            m_client->m_wrapper.grabButton(XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC);
            return;
        }

        // When a passive grab is activated or deactivated, the X server will generate crossing
        // events as if the pointer were suddenly to warp from its current position to some position
        // in the grab window. Some /broken/ X11 clients do get confused by such EnterNotify and
        // LeaveNotify events so we release the passive grab for the active window.
        //
        // The passive grab below is established so the window can be raised or activated when it
        // is clicked.
        if ((options->focusPolicyIsReasonable() && !active()) ||
                (options->isClickRaise() && !win::is_most_recently_raised(m_client))) {
            if (options->commandWindow1() != Options::MouseNothing) {
                m_client->establishCommandWindowGrab(XCB_BUTTON_INDEX_1);
            }
            if (options->commandWindow2() != Options::MouseNothing) {
                m_client->establishCommandWindowGrab(XCB_BUTTON_INDEX_2);
            }
            if (options->commandWindow3() != Options::MouseNothing) {
                m_client->establishCommandWindowGrab(XCB_BUTTON_INDEX_3);
            }
            if (options->commandWindowWheel() != Options::MouseNothing) {
                m_client->establishCommandWindowGrab(XCB_BUTTON_INDEX_4);
                m_client->establishCommandWindowGrab(XCB_BUTTON_INDEX_5);
            }
        }

        // We want to grab <command modifier> + buttons no matter what state the window is in. The
        // client will receive funky EnterNotify and LeaveNotify events, but there is nothing that
        // we can do about it, unfortunately.

        if (!workspace()->globalShortcutsDisabled()) {
            if (options->commandAll1() != Options::MouseNothing) {
                m_client->establishCommandAllGrab(XCB_BUTTON_INDEX_1);
            }
            if (options->commandAll2() != Options::MouseNothing) {
                m_client->establishCommandAllGrab(XCB_BUTTON_INDEX_2);
            }
            if (options->commandAll3() != Options::MouseNothing) {
                m_client->establishCommandAllGrab(XCB_BUTTON_INDEX_3);
            }
            if (options->commandAllWheel() != Options::MouseWheelNothing) {
                m_client->establishCommandAllGrab(XCB_BUTTON_INDEX_4);
                m_client->establishCommandAllGrab(XCB_BUTTON_INDEX_5);
            }
        }
    }

    void destroy_decoration() override
    {
        QRect oldgeom = m_client->frameGeometry();
        if (win::decoration(m_client)) {
            auto grav = m_client->calculateGravitation(true);
            control::destroy_decoration();
            m_client->plainResize(m_client->sizeForClientSize(m_client->clientSize()),
                                  win::force_geometry::yes);
            win::move(m_client, grav);
            if (win::compositing())
                m_client->discardWindowPixmap();
            if (!m_client->deleting) {
                Q_EMIT m_client->geometryShapeChanged(m_client, oldgeom);
            }
        }
        m_client->m_decoInputExtent.reset();
    }

    bool prepare_move(QPoint const& target, win::force_geometry force) override
    {
        m_client->m_clientGeometry.moveTopLeft(m_client->framePosToClientPos(target));
        auto const bufferPosition
            = win::decoration(m_client) ? target : m_client->m_clientGeometry.topLeft();

        if (!geometry_updates_blocked() && target != rules().checkPosition(target)) {
            qCDebug(KWIN_CORE) << "Ruled position fails:" << target << ":"
                               << rules().checkPosition(target);
        }

        auto geo = m_client->frameGeometry();
        geo.moveTopLeft(target);
        m_client->set_frame_geometry(geo);

        if (force == win::force_geometry::no && m_client->m_bufferGeometry.topLeft() == bufferPosition) {
            return false;
        }

        m_client->m_bufferGeometry.moveTopLeft(bufferPosition);
        return true;
    }

    void do_move() override
    {
        m_client->updateServerGeometry();
    }

    bool can_fullscreen() const override
    {
        if (!rules().checkFullScreen(true)) {
            return false;
        }
        if (rules().checkStrictGeometry(true)) {
            // check geometry constraints (rule to obey is set)
            const QRect fsarea = workspace()->clientArea(FullScreenArea, m_client);
            if (m_client->sizeForClientSize(fsarea.size(), win::size_mode::any, true) != fsarea.size()) {
                // the app wouldn't fit exactly fullscreen geometry due to its strict geometry requirements
                return false;
            }
        }
        // don't check size constrains - some apps request fullscreen despite requesting fixed size
        // also better disallow weird types to go fullscreen
        return !win::is_special_window(m_client);
    }

private:
    X11Client* m_client;
};

constexpr long ClientWinMask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_KEYMAP_STATE
    | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION | // need this, too!
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
    | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

// window types that are supported as normal windows (i.e. KWin actually manages them)
constexpr NET::WindowTypes SUPPORTED_MANAGED_WINDOW_TYPES_MASK = NET::NormalMask | NET::DesktopMask
    | NET::DockMask | NET::ToolbarMask | NET::MenuMask
    | NET::DialogMask /*| NET::OverrideMask*/ | NET::TopMenuMask | NET::UtilityMask
    | NET::SplashMask | NET::NotificationMask | NET::OnScreenDisplayMask
    | NET::CriticalNotificationMask;

// Creating a client:
//  - only by calling Workspace::createClient()
//      - it creates a new client and calls manage() for it
//
// Destroying a client:
//  - destroyClient() - only when the window itself has been destroyed
//      - releaseWindow() - the window is kept, only the client itself is destroyed

/**
 * \class Client x11client.h
 * \brief The Client class encapsulates a window decoration frame.
 */

/**
 * This ctor is "dumb" - it only initializes data. All the real initialization
 * is done in manage().
 */
X11Client::X11Client()
    : Toplevel(new x11_transient(this))
    , m_control{std::make_unique<x11_control>(this)}
    , m_client()
    , m_wrapper()
    , m_frame()
    , m_moveResizeGrabWindow()
    , m_motif(atoms->motif_wm_hints)
    , allowed_actions()
    , m_decoInputExtent()
{
    supported_default_types = SUPPORTED_MANAGED_WINDOW_TYPES_MASK;
    has_in_content_deco = true;

    win::setup_connections(this);
    m_control->setup_tabbox();
    m_control->setup_color_scheme();

    m_syncRequest.lastTimestamp = xTime();

    info = nullptr;

    // So that decorations don't start with size being (0,0).
    set_frame_geometry(QRect(0, 0, 100, 100));

    connect(clientMachine(), &ClientMachine::localhostChanged, this, &X11Client::updateCaption);
    connect(options, &Options::configChanged, this, [this] { control()->update_mouse_grab(); });
    connect(options, &Options::condensedTitleChanged, this, &X11Client::updateCaption);

    connect(this, &X11Client::moveResizeCursorChanged, this, [this] (CursorShape cursor) {
        xcb_cursor_t nativeCursor = Cursor::x11Cursor(cursor);
        m_frame.defineCursor(nativeCursor);
        if (m_decoInputExtent.isValid())
            m_decoInputExtent.defineCursor(nativeCursor);
        if (control()->move_resize().enabled) {
            // changing window attributes doesn't change cursor if there's pointer grab active
            xcb_change_active_pointer_grab(connection(), nativeCursor, xTime(),
                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW);
        }
    });
}

/**
 * "Dumb" destructor.
 */
X11Client::~X11Client()
{
    if (m_killHelperPID && !::kill(m_killHelperPID, 0)) {
        // Means the process is alive.
        ::kill(m_killHelperPID, SIGTERM);
        m_killHelperPID = 0;
    }
    if (m_syncRequest.alarm != XCB_NONE) {
        xcb_sync_destroy_alarm(connection(), m_syncRequest.alarm);
    }
    Q_ASSERT(!control()->move_resize().enabled);
    Q_ASSERT(m_client == XCB_WINDOW_NONE);
    Q_ASSERT(m_wrapper == XCB_WINDOW_NONE);
    Q_ASSERT(m_frame == XCB_WINDOW_NONE);
    for (auto it = m_connections.constBegin(); it != m_connections.constEnd(); ++it) {
        disconnect(*it);
    }
}

win::control* X11Client::control() const
{
    return m_control.get();
}

// Use destroyClient() or releaseWindow(), Client instances cannot be deleted directly
void X11Client::deleteClient(X11Client *c)
{
    delete c;
}

/**
 * Releases the window. The client has done its job and the window is still existing.
 */
void X11Client::releaseWindow(bool on_shutdown)
{
    Q_ASSERT(!deleting);
    deleting = true;

#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox *tabBox = TabBox::TabBox::self();
    if (tabBox->isDisplayed() && tabBox->currentClient() == this) {
        tabBox->nextPrev(true);
    }
#endif

    control()->destroy_wayland_management();

    Toplevel* del = nullptr;
    if (!on_shutdown) {
        del = create_remnant(this);
    }

    if (control()->move_resize().enabled) {
        Q_EMIT clientFinishUserMovedResized(this);
    }

    Q_EMIT windowClosed(this, del);
    finishCompositing();

    // Remove ForceTemporarily rules
    RuleBook::self()->discardUsed(this, true);

    StackingUpdatesBlocker blocker(workspace());
    if (control()->move_resize().enabled) {
        leaveMoveResize();
    }

    win::finish_rules(this);
    control()->block_geometry_updates();

    if (isOnCurrentDesktop() && isShown(true)) {
        addWorkspaceRepaint(win::visible_rect(this));
    }

    // Grab X during the release to make removing of properties, setting to withdrawn state
    // and repareting to root an atomic operation (https://lists.kde.org/?l=kde-devel&m=116448102901184&w=2)
    grabXServer();
    exportMappingState(XCB_ICCCM_WM_STATE_WITHDRAWN);

    // So that it's not considered visible anymore (can't use hideClient(), it would set flags)
    hidden = true;

    if (!on_shutdown) {
        workspace()->clientHidden(this);
    }

    // Destroying decoration would cause ugly visual effect
    m_frame.unmap();

    control()->destroy_decoration();
    cleanGrouping();

    if (!on_shutdown) {
        workspace()->removeClient(this);
        // Only when the window is being unmapped, not when closing down KWin (NETWM sections 5.5,5.7)
        info->setDesktop(0);
        info->setState(NET::States(), info->state());  // Reset all state flags
    }

    m_client.deleteProperty(atoms->kde_net_wm_user_creation_time);
    m_client.deleteProperty(atoms->net_frame_extents);
    m_client.deleteProperty(atoms->kde_net_wm_frame_strut);

    m_client.reparent(rootWindow(), m_bufferGeometry.x(), m_bufferGeometry.y());
    xcb_change_save_set(connection(), XCB_SET_MODE_DELETE, m_client);
    m_client.selectInput(XCB_EVENT_MASK_NO_EVENT);

    if (on_shutdown) {
        // Map the window, so it can be found after another WM is started
        m_client.map();
        // TODO: Preserve minimized, shaded etc. state?
    } else {
        // Make sure it's not mapped if the app unmapped it (#65279). The app
        // may do map+unmap before we initially map the window by calling rawShow() from manage().
        m_client.unmap();
    }

    m_client.reset();
    m_wrapper.reset();
    m_frame.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    control()->unblock_geometry_updates();

    if (!on_shutdown) {
        disownDataPassedToDeleted();
        del->remnant()->unref();
    }

    deleteClient(this);
    ungrabXServer();
}

/**
 * Like releaseWindow(), but this one is called when the window has been already destroyed
 * (E.g. The application closed it)
 */
void X11Client::destroyClient()
{
    Q_ASSERT(!deleting);
    deleting = true;

#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox *tabBox = TabBox::TabBox::self();
    if (tabBox && tabBox->isDisplayed() && tabBox->currentClient() == this) {
        tabBox->nextPrev(true);
    }
#endif

    control()->destroy_wayland_management();

    auto del = create_remnant(this);

    if (control()->move_resize().enabled) {
        Q_EMIT clientFinishUserMovedResized(this);
    }
    Q_EMIT windowClosed(this, del);

    finishCompositing(ReleaseReason::Destroyed);

    // Remove ForceTemporarily rules
    RuleBook::self()->discardUsed(this, true);

    StackingUpdatesBlocker blocker(workspace());
    if (control()->move_resize().enabled) {
        leaveMoveResize();
    }

    win::finish_rules(this);
    control()->block_geometry_updates();

    if (isOnCurrentDesktop() && isShown(true)) {
        addWorkspaceRepaint(win::visible_rect(this));
    }

    // So that it's not considered visible anymore
    hidden = true;

    workspace()->clientHidden(this);
    control()->destroy_decoration();
    cleanGrouping();
    workspace()->removeClient(this);

    // invalidate
    m_client.reset();
    m_wrapper.reset();
    m_frame.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    control()->unblock_geometry_updates();
    disownDataPassedToDeleted();
    del->remnant()->unref();
    deleteClient(this);
}

/**
 * Manages the clients. This means handling the very first maprequest:
 * reparenting, initial geometry, initial state, placement, etc.
 * Returns false if KWin is not going to manage this window.
 */
bool X11Client::manage(xcb_window_t w, bool isMapped)
{
    StackingUpdatesBlocker stacking_blocker(workspace());

    Xcb::WindowAttributes attr(w);
    Xcb::WindowGeometry windowGeometry(w);
    if (attr.isNull() || windowGeometry.isNull()) {
        return false;
    }

    // From this place on, manage() must not return false
    control()->block_geometry_updates();

    // Force update when finishing with geometry changes
    control()->set_pending_geometry_update(win::pending_geometry::forced);

    embedClient(w, attr->visual, attr->colormap, windowGeometry->depth);

    m_visual = attr->visual;
    bit_depth = windowGeometry->depth;

    // SELI TODO: Order all these things in some sane manner

    const NET::Properties properties =
        NET::WMDesktop |
        NET::WMState |
        NET::WMWindowType |
        NET::WMStrut |
        NET::WMName |
        NET::WMIconGeometry |
        NET::WMIcon |
        NET::WMPid |
        NET::WMIconName;
    const NET::Properties2 properties2 =
        NET::WM2BlockCompositing |
        NET::WM2WindowClass |
        NET::WM2WindowRole |
        NET::WM2UserTime |
        NET::WM2StartupId |
        NET::WM2ExtendedStrut |
        NET::WM2Opacity |
        NET::WM2FullscreenMonitors |
        NET::WM2GroupLeader |
        NET::WM2Urgency |
        NET::WM2Input |
        NET::WM2Protocols |
        NET::WM2InitialMappingState |
        NET::WM2IconPixmap |
        NET::WM2OpaqueRegion |
        NET::WM2DesktopFileName |
        NET::WM2GTKFrameExtents;

    auto wmClientLeaderCookie = fetchWmClientLeader();
    auto skipCloseAnimationCookie = win::x11::fetch_skip_close_animation(window());
    auto showOnScreenEdgeCookie = fetchShowOnScreenEdge();
    auto colorSchemeCookie = fetchColorScheme();
    auto firstInTabBoxCookie = fetchFirstInTabBox();
    auto transientCookie = fetchTransient();
    auto activitiesCookie = fetchActivities();
    auto applicationMenuServiceNameCookie = fetchApplicationMenuServiceName();
    auto applicationMenuObjectPathCookie = fetchApplicationMenuObjectPath();

    m_geometryHints.init(window());
    m_motif.init(window());

    info = new WinInfo(this, m_client, rootWindow(), properties, properties2);

    if (win::is_desktop(this) && bit_depth == 32) {
        // force desktop windows to be opaque. It's a desktop after all, there is no window below
        bit_depth = 24;
    }

    // If it's already mapped, ignore hint
    bool init_minimize = !isMapped && (info->initialMappingState() == NET::Iconic);

    m_colormap = attr->colormap;

    getResourceClass();
    readWmClientLeader(wmClientLeaderCookie);
    getWmClientMachine();
    getSyncCounter();

    // First only read the caption text, so that win::setup_rules(..) can use it for matching,
    // and only then really set the caption using setCaption(), which checks for duplicates etc.
    // and also relies on rules already existing
    cap_normal = readName();

    win::setup_rules(this, false);
    setCaption(cap_normal, true);

    connect(this, &X11Client::windowClassChanged, this, [this] { win::evaluate_rules(this); });

    if (Xcb::Extensions::self()->isShapeAvailable()) {
        xcb_shape_select_input(connection(), window(), true);
    }

    detectShape(window());
    detectNoBorder();
    fetchIconicName();
    setClientFrameExtents(info->gtkFrameExtents());

    // Needs to be done before readTransient() because of reading the group
    checkGroup(nullptr);
    updateUrgency();

    // Group affects isMinimizable()
    updateAllowedActions();

    // Needs to be valid before handling groups
    transient()->set_modal((info->state() & NET::Modal) != 0);
    readTransientProperty(transientCookie);

    win::set_desktop_file_name(this,
        control()->rules().checkDesktopFile(QByteArray(info->desktopFileName()), true).toUtf8());
    getIcons();

    connect(this, &X11Client::desktopFileNameChanged, this, &X11Client::getIcons);

    m_geometryHints.read();
    getMotifHints();
    getWmOpaqueRegion();
    setSkipCloseAnimation(skipCloseAnimationCookie.toBool());

    // TODO: Try to obey all state information from info->state()

    win::set_original_skip_taskbar(this, (info->state() & NET::SkipTaskbar) != 0);
    win::set_skip_pager(this, (info->state() & NET::SkipPager) != 0);
    win::set_skip_switcher(this, (info->state() & NET::SkipSwitcher) != 0);
    readFirstInTabBox(firstInTabBoxCookie);

    setupCompositing(false);

    KStartupInfoId asn_id;
    KStartupInfoData asn_data;
    bool asn_valid = workspace()->checkStartupNotification(window(), asn_id, asn_data);

    // Make sure that the input window is created before we update the stacking order
    updateInputWindow();

    workspace()->updateClientLayer(this);

    auto session = workspace()->takeSessionInfo(this);
    if (session) {
        init_minimize = session->minimized;
        noborder = session->noBorder;
    }

    win::set_shortcut(this, control()->rules().checkShortcut(session ? session->shortcut :
                                                                       QString(), true));

    init_minimize = control()->rules().checkMinimize(init_minimize, !isMapped);
    noborder = control()->rules().checkNoBorder(noborder, !isMapped);

    readActivities(activitiesCookie);

    // Initial desktop placement
    int desk = 0;
    if (session) {
        desk = session->desktop;
        if (session->onAllDesktops) {
            desk = NET::OnAllDesktops;
        }
        setOnActivities(session->activities);
    } else {
        // If this window is transient, ensure that it is opened on the
        // same window as its parent.  this is necessary when an application
        // starts up on a different desktop than is currently displayed.
        if (isTransient()) {
            auto leads = transient()->leads();
            bool on_current = false;
            bool on_all = false;
            Toplevel* maincl = nullptr;

            // This is slightly duplicated from Placement::placeOnMainWindow()
            for (auto const& lead : leads) {
                if (leads.size() > 1 && win::is_special_window(lead) &&
                        !(info->state() & NET::Modal)) {
                    // Don't consider group-transients and toolbars etc when placing
                    // except when it's modal (blocks specials as well).
                    continue;
                }

                maincl = lead;
                if (lead->isOnCurrentDesktop()) {
                    on_current = true;
                }
                if (lead->isOnAllDesktops()) {
                    on_all = true;
                }
            }

            if (on_all) {
                desk = NET::OnAllDesktops;
            } else if (on_current) {
                desk = VirtualDesktopManager::self()->current();
            } else if (maincl != nullptr) {
                desk = maincl->desktop();
            }

            if (maincl) {
                setOnActivities(maincl->activities());
            }
        } else {
            // A transient shall appear on its leader and not drag that around.
            if (info->desktop()) {
                // Window had the initial desktop property, force it
                desk = info->desktop();
            }
            if (desktop() == 0 && asn_valid && asn_data.desktop() != 0) {
                desk = asn_data.desktop();
            }
        }
#ifdef KWIN_BUILD_ACTIVITIES
        if (Activities::self() && !isMapped && !noborder && win::is_normal(this) && !activitiesDefined) {
            //a new, regular window, when we're not recovering from a crash,
            //and it hasn't got an activity. let's try giving it the current one.
            //TODO: decide whether to keep this before the 4.6 release
            //TODO: if we are keeping it (at least as an option), replace noborder checking
            //with a public API for setting windows to be on all activities.
            //something like KWindowSystem::setOnAllActivities or
            //KActivityConsumer::setOnAllActivities
            setOnActivity(Activities::self()->current(), true);
        }
#endif
    }

    if (desk == 0) {
        // Assume window wants to be visible on the current desktop
        desk = win::is_desktop(this) ? static_cast<int>(NET::OnAllDesktops) :
                                       VirtualDesktopManager::self()->current();
    }
    desk = control()->rules().checkDesktop(desk, !isMapped);

    if (desk != NET::OnAllDesktops) {
        // Do range check
        desk = qBound(1, desk, static_cast<int>(VirtualDesktopManager::self()->count()));
    }

    win::set_desktop(this, desk);
    info->setDesktop(desk);

    // SELI TODO
    workspace()->updateOnAllDesktopsOfTransients(this);
    //onAllDesktopsChange(); // Decoration doesn't exist here yet

    QString activitiesList;
    activitiesList = control()->rules().checkActivity(activitiesList, !isMapped);
    if (!activitiesList.isEmpty()) {
        setOnActivities(activitiesList.split(QStringLiteral(",")));
    }

    QRect geom(windowGeometry.rect());
    auto placementDone = false;

    if (session) {
        geom = session->geometry;
    }

    QRect area;
    auto partial_keep_in_area = isMapped || session;

    if (isMapped || session) {
        area = workspace()->clientArea(FullArea, geom.center(), desktop());
        win::check_offscreen_position(&geom, area);
    } else {
        int screen = asn_data.xinerama() == -1 ? screens()->current() : asn_data.xinerama();
        screen = control()->rules().checkScreen(screen, !isMapped);
        area = workspace()->clientArea(PlacementArea, screens()->geometry(screen).center(), desktop());
    }

    if (win::is_desktop(this)) {
        // KWin doesn't manage desktop windows
        placementDone = true;
    }

    auto usePosition = false;

    if (isMapped || session || placementDone) {
        // Use geometry.
        placementDone = true;
    } else if (isTransient() && !win::is_utility(this) && !win::is_dialog(this) &&
               !win::is_splash(this)) {
        usePosition = true;
    } else if (isTransient() && !hasNETSupport()) {
        usePosition = true;
    } else if (win::is_dialog(this) && hasNETSupport()) {
        // If the dialog is actually non-NETWM transient window, don't try to apply placement to it,
        // it breaks with too many things (xmms, display)
        if (transient()->lead()) {
#if 1
            // #78082 - Ok, it seems there are after all some cases when an application has a good
            // reason to specify a position for its dialog. Too bad other WMs have never bothered
            // with placement for dialogs, so apps always specify positions for their dialogs,
            // including such silly positions like always centered on the screen or under mouse.
            // Using ignoring requested position in window-specific settings helps, and now
            // there's also _NET_WM_FULL_PLACEMENT.
            usePosition = true;
#else
            ; // Force using placement policy
#endif
        } else {
            usePosition = true;
        }
    } else if (win::is_splash(this)) {
        ; // Force using placement policy
    } else {
        usePosition = true;
    }

    if (!control()->rules().checkIgnoreGeometry(!usePosition, true)) {
        if (m_geometryHints.hasPosition()) {
            placementDone = true;
            // Disobey xinerama placement option for now (#70943)
            area = workspace()->clientArea(PlacementArea, geom.center(), desktop());
        }
    }

    if (isMovable() && (geom.x() > area.right() || geom.y() > area.bottom())) {
        placementDone = false; // Weird, do not trust.
    }

    if (placementDone) {
        auto position = geom.topLeft();

        // Session contains the position of the frame geometry before gravitating.
        if (!session) {
            position = clientPosToFramePos(position);
        }
        win::move(this, position);
    }

    // Create client group if the window will have a decoration
    auto dontKeepInArea = false;
    readColorScheme(colorSchemeCookie);

    readApplicationMenuServiceName(applicationMenuServiceNameCookie);
    readApplicationMenuObjectPath(applicationMenuObjectPathCookie);

    // Also gravitates
    updateDecoration(false);

    // TODO: Is CentralGravity right here, when resizing is done after gravitating?
    plainResize(control()->rules().checkSize(sizeForClientSize(geom.size()), !isMapped));

    auto forced_pos = control()->rules().checkPosition(invalidPoint, !isMapped);
    if (forced_pos != invalidPoint) {
        win::move(this, forced_pos);
        placementDone = true;
        // Don't keep inside workarea if the window has specially configured position
        partial_keep_in_area = true;
        area = workspace()->clientArea(FullArea, geom.center(), desktop());
    }

    if (!placementDone) {
        // Placement needs to be after setting size
        Placement::self()->place(this, area);
        // The client may have been moved to another screen, update placement area.
        area = workspace()->clientArea(PlacementArea, this);
        dontKeepInArea = true;
        placementDone = true;
    }

    // bugs #285967, #286146, #183694
    // geometry() now includes the requested size and the decoration and is at the correct screen/position (hopefully)
    // Maximization for oversized windows must happen NOW.
    // If we effectively pass keepInArea(), the window will resizeWithChecks() - i.e. constrained
    // to the combo of all screen MINUS all struts on the edges
    // If only one screen struts, this will affect screens as a side-effect, the window is artificailly shrinked
    // below the screen size and as result no more maximized what breaks KMainWindow's stupid width+1, height+1 hack
    // TODO: get KMainWindow a correct state storage what will allow to store the restore size as well.

    if (!session) {
        // Has a better handling of this.
        // First remember restore geometry.
        restore_geometries.maximize = frameGeometry();

        if (isMaximizable() &&
                (size().width() >= area.width() || size().height() >= area.height())) {
            // Window is too large for the screen, maximize in the
            // directions necessary
            auto const ss = workspace()->clientArea(ScreenArea, area.center(), desktop()).size();
            auto const fsa = workspace()->clientArea(FullArea, geom.center(), desktop());
            auto const cs = clientSize();

            auto pseudo_max{win::maximize_mode::restore};
            if (info->state() & NET::MaxVert) {
                pseudo_max = pseudo_max | win::maximize_mode::vertical;
            }
            if (info->state() & NET::MaxHoriz) {
                pseudo_max = pseudo_max | win::maximize_mode::horizontal;
            }

            if (size().width() >= area.width()) {
                pseudo_max = pseudo_max | win::maximize_mode::horizontal;
            }
            if (size().height() >= area.height()) {
                pseudo_max = pseudo_max | win::maximize_mode::vertical;
            }

            // heuristics:
            // if decorated client is smaller than the entire screen, the user might want to move it around (multiscreen)
            // in this case, if the decorated client is bigger than the screen (+1), we don't take this as an
            // attempt for maximization, but just constrain the size (the window simply wants to be bigger)
            // NOTICE
            // i intended a second check on cs < area.size() ("the managed client ("minus border") is smaller
            // than the workspace") but gtk / gimp seems to store it's size including the decoration,
            // thus a former maximized window wil become non-maximized
            bool keepInFsArea = false;

            if (size().width() < fsa.width() && (cs.width() > ss.width()+1)) {
                pseudo_max = pseudo_max & ~win::maximize_mode::horizontal;
                keepInFsArea = true;
            }
            if (size().height() < fsa.height() && (cs.height() > ss.height()+1)) {
                pseudo_max = pseudo_max & ~win::maximize_mode::vertical;
                keepInFsArea = true;
            }

            if (static_cast<win::maximize_mode>(pseudo_max) != win::maximize_mode::restore) {
                win::maximize(this, static_cast<win::maximize_mode>(pseudo_max));
                // from now on, care about maxmode, since the maximization call will override mode for fix aspects
                dontKeepInArea |= (max_mode == win::maximize_mode::full);

                // Use placement when unmaximizing ...
                restore_geometries.maximize = QRect();

                if ((max_mode & win::maximize_mode::vertical) != win::maximize_mode::vertical) {
                    // ...but only for horizontal direction
                    restore_geometries.maximize.setY(pos().y());
                    restore_geometries.maximize.setHeight(size().height());
                }
                if ((max_mode & win::maximize_mode::horizontal) != win::maximize_mode::horizontal) {
                    // ...but only for vertical direction
                    restore_geometries.maximize.setX(pos().x());
                    restore_geometries.maximize.setWidth(size().width());
                }
            }
            if (keepInFsArea) {
                win::keep_in_area(this, fsa, partial_keep_in_area);
            }
        }
    }

    if ((!win::is_special_window(this) || win::is_toolbar(this)) && isMovable() &&
            !dontKeepInArea) {
        win::keep_in_area(this, area, partial_keep_in_area);
    }

    updateShape();

    // CT: Extra check for stupid jdk 1.3.1. But should make sense in general
    // if client has initial state set to Iconic and is transient with a parent
    // window that is not Iconic, set init_state to Normal
    if (init_minimize) {
        auto leads = transient()->leads();
        for (auto lead : leads) {
            if (lead->isShown(true)) {
                // SELI TODO: Even e.g. for NET::Utility?
                init_minimize = false;
            }
        }
    }

    // If a dialog is shown for minimized window, minimize it too
    if (!init_minimize && transient()->lead() &&
            workspace()->sessionManager()->state() != SessionState::Saving) {
        bool visible_parent = false;

        for (auto const& lead : transient()->leads()) {
            if (lead->isShown(true)) {
                visible_parent = true;
            }
        }

        if (!visible_parent) {
            init_minimize = true;
            win::set_demands_attention(this, true);
        }
    }

    if (init_minimize) {
        win::set_minimized(this, true, true);
    }

    // Other settings from the previous session
    if (session) {
        // Session restored windows are not considered to be new windows WRT rules,
        // I.e. obey only forcing rules
        win::set_keep_above(this, session->keepAbove);
        win::set_keep_below(this, session->keepBelow);
        win::set_original_skip_taskbar(this, session->skipTaskbar);
        win::set_skip_pager(this, session->skipPager);
        win::set_skip_switcher(this, session->skipSwitcher);
        setShade(session->shaded ? win::shade::normal : win::shade::none);
        setOpacity(session->opacity);

        restore_geometries.maximize = session->restore;

        if (static_cast<win::maximize_mode>(session->maximized) != win::maximize_mode::restore) {
            win::maximize(this, static_cast<win::maximize_mode>(session->maximized));
        }
        if (session->fullscreen) {
            setFullScreen(true, false);
            restore_geometries.fullscreen = session->fsrestore;
        }

        win::check_offscreen_position(&restore_geometries.maximize, area);
        win::check_offscreen_position(&restore_geometries.fullscreen, area);

    } else {
        // Window may want to be maximized
        // done after checking that the window isn't larger than the workarea, so that
        // the restore geometry from the checks above takes precedence, and window
        // isn't restored larger than the workarea
        auto maxmode{win::maximize_mode::restore};

        if (info->state() & NET::MaxVert) {
            maxmode = maxmode | win::maximize_mode::vertical;
        }
        if (info->state() & NET::MaxHoriz) {
            maxmode = maxmode | win::maximize_mode::horizontal;
        }

        auto forced_maxmode = control()->rules().checkMaximize(maxmode, !isMapped);

        // Either hints were set to maximize, or is forced to maximize,
        // or is forced to non-maximize and hints were set to maximize
        if (forced_maxmode != win::maximize_mode::restore ||
                maxmode != win::maximize_mode::restore) {
            win::maximize(this, forced_maxmode);
        }

        // Read other initial states
        setShade(control()->rules().checkShade(
            info->state() & NET::Shaded ? win::shade::normal : win::shade::none, !isMapped));
        win::set_keep_above(
            this, control()->rules().checkKeepAbove(info->state() & NET::KeepAbove, !isMapped));
        win::set_keep_below(
            this, control()->rules().checkKeepBelow(info->state() & NET::KeepBelow, !isMapped));
        win::set_original_skip_taskbar(
            this, control()->rules().checkSkipTaskbar(info->state() & NET::SkipTaskbar, !isMapped));
        win::set_skip_pager(
            this, control()->rules().checkSkipPager(info->state() & NET::SkipPager, !isMapped));
        win::set_skip_switcher(
            this,
            control()->rules().checkSkipSwitcher(info->state() & NET::SkipSwitcher, !isMapped));

        if (info->state() & NET::DemandsAttention) {
            control()->demands_attention();
        }
        if (info->state() & NET::Modal) {
            transient()->set_modal(true);
        }

        setFullScreen(control()->rules().checkFullScreen(info->state() & NET::FullScreen,
                                                         !isMapped), false);
    }

    updateAllowedActions(true);

    // Set initial user time directly
    m_userTime = readUserTimeMapTimestamp(asn_valid ? &asn_id : nullptr,
                                          asn_valid ? &asn_data : nullptr, session);

    // And do what X11Client::updateUserTime() does
    group()->updateUserTime(m_userTime);

    // This should avoid flicker, because real restacking is done
    // only after manage() finishes because of blocking, but the window is shown sooner
    m_frame.lower();

    if (session && session->stackingOrder != -1) {
        sm_stacking_order = session->stackingOrder;
        workspace()->restoreSessionStackingOrder(this);
    }

    if (win::compositing()) {
        // Sending ConfigureNotify is done when setting mapping state below,
        // Getting the first sync response means window is ready for compositing
        sendSyncRequest();
    } else {
        // set to true in case compositing is turned on later. bug #160393
        ready_for_painting = true;
    }

    if (isShown(true)) {
        bool allow;
        if (session) {
            allow = session->active &&
                    (!workspace()->wasUserInteraction() || workspace()->activeClient() == nullptr ||
                     win::is_desktop(workspace()->activeClient()));
        } else {
            allow = workspace()->allowClientActivation(this, userTime(), false);
        }

        auto const isSessionSaving = workspace()->sessionManager()->state() == SessionState::Saving;

        // If session saving, force showing new windows (i.e. "save file?" dialogs etc.)
        // also force if activation is allowed
        if( !isOnCurrentDesktop() && !isMapped && !session && ( allow || isSessionSaving )) {
            VirtualDesktopManager::self()->setCurrent( desktop());
        }

        // If the window is on an inactive activity during session saving, temporarily force it to show.
        if(!isMapped && !session && isSessionSaving && !isOnCurrentActivity()) {
            setSessionActivityOverride(true);
            for (auto mc : transient()->leads()) {
                if (auto x11_mc = dynamic_cast<X11Client*>(mc)) {
                    x11_mc->setSessionActivityOverride(true);
                }
            }
        }

        if (isOnCurrentDesktop() && !isMapped && !allow && (!session || session->stackingOrder < 0)) {
            workspace()->restackClientUnderActive(this);
        }

        updateVisibility();

        if (!isMapped) {
            if (allow && isOnCurrentDesktop()) {
                if (!win::is_special_window(this)) {
                    if (options->focusPolicyIsReasonable() && win::wants_tab_focus(this)) {
                        workspace()->request_focus(this);
                    }
                }
            } else if (!session && !win::is_special_window(this)) {
                control()->demands_attention();
            }
        }
    } else {
        updateVisibility();
    }

    Q_ASSERT(mapping_state != Withdrawn);
    m_managed = true;
    win::block_geometry_updates(this, false);

    if (m_userTime == XCB_TIME_CURRENT_TIME || m_userTime == -1U) {
        // No known user time, set something old
        m_userTime = xTime() - 1000000;

        // Let's be paranoid.
        if (m_userTime == XCB_TIME_CURRENT_TIME || m_userTime == -1U) {
            m_userTime = xTime() - 1000000 + 10;
        }
    }

    // Done when setting mapping state
    //sendSyntheticConfigureNotify();

    delete session;

    control()->discard_temporary_rules();

    // Just in case
    applyWindowRules();

    // Remove ApplyNow rules
    RuleBook::self()->discardUsed(this, false);

    // Was blocked while !isManaged()
    updateWindowRules(Rules::All);

    setBlockingCompositing(info->isBlockingCompositing());
    readShowOnScreenEdge(showOnScreenEdgeCookie);

    // Forward all opacity values to the frame in case there'll be other CM running.
    connect(Compositor::self(), &Compositor::compositingToggled, this,
        [this](bool active) {
            if (active) {
                return;
            }
            if (opacity() == 1.0) {
                return;
            }
            NETWinInfo info(connection(), frameId(), rootWindow(), NET::Properties(), NET::Properties2());
            info.setOpacity(static_cast<unsigned long>(opacity() * 0xffffffff));
        }
    );

    // TODO: there's a small problem here - isManaged() depends on the mapping state,
    // but this client is not yet in Workspace's client list at this point, will
    // be only done in addClient()
    Q_EMIT clientManaging(this);
    return true;
}

// Called only from manage()
void X11Client::embedClient(xcb_window_t w, xcb_visualid_t visualid, xcb_colormap_t colormap, uint8_t depth)
{
    Q_ASSERT(m_client == XCB_WINDOW_NONE);
    Q_ASSERT(frameId() == XCB_WINDOW_NONE);
    Q_ASSERT(m_wrapper == XCB_WINDOW_NONE);
    m_client.reset(w, false);

    const uint32_t zero_value = 0;

    xcb_connection_t *conn = connection();

    // We don't want the window to be destroyed when we quit
    xcb_change_save_set(conn, XCB_SET_MODE_INSERT, m_client);

    m_client.selectInput(zero_value);
    m_client.unmap();
    m_client.setBorderWidth(zero_value);

    // Note: These values must match the order in the xcb_cw_t enum
    const uint32_t cw_values[] = {
        0,        // back_pixmap
        0,        // border_pixel
        colormap, // colormap
        Cursor::x11Cursor(Qt::ArrowCursor),
    };

    auto const cw_mask = XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL |
                             XCB_CW_COLORMAP | XCB_CW_CURSOR;

    auto const common_event_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                       XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
                                       XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                       XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION |
                                       XCB_EVENT_MASK_KEYMAP_STATE |
                                       XCB_EVENT_MASK_FOCUS_CHANGE |
                                       XCB_EVENT_MASK_EXPOSURE |
                                       XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

    auto const frame_event_mask   = common_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_VISIBILITY_CHANGE;
    auto const wrapper_event_mask = common_event_mask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

    auto const client_event_mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE |
                                       XCB_EVENT_MASK_COLOR_MAP_CHANGE |
                                       XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW |
                                       XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

    // Create the frame window
    auto frame = xcb_generate_id(conn);
    xcb_create_window(conn, depth, frame, rootWindow(), 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, visualid, cw_mask, cw_values);
    m_frame.reset(frame);

    setWindowHandles(m_client);

    // Create the wrapper window
    auto wrapperId = xcb_generate_id(conn);
    xcb_create_window(conn, depth, wrapperId, frame, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, visualid, cw_mask, cw_values);

    m_wrapper.reset(wrapperId);
    m_client.reparent(m_wrapper);

    // We could specify the event masks when we create the windows, but the original
    // Xlib code didn't.  Let's preserve that behavior here for now so we don't end up
    // receiving any unexpected events from the wrapper creation or the reparenting.
    m_frame.selectInput(frame_event_mask);
    m_wrapper.selectInput(wrapper_event_mask);
    m_client.selectInput(client_event_mask);

    control()->update_mouse_grab();
}

void X11Client::updateInputWindow()
{
    if (!Xcb::Extensions::self()->isShapeInputAvailable()) {
        return;
    }

    QRegion region;

    if (!noBorder() && win::decoration(this)) {
        const QMargins &r = win::decoration(this)->resizeOnlyBorders();
        const int left   = r.left();
        const int top    = r.top();
        const int right  = r.right();
        const int bottom = r.bottom();
        if (left != 0 || top != 0 || right != 0 || bottom != 0) {
            region = QRegion(-left,
                             -top,
                             win::decoration(this)->size().width() + left + right,
                             win::decoration(this)->size().height() + top + bottom);
            region = region.subtracted(win::decoration(this)->rect());
        }
    }

    if (region.isEmpty()) {
        m_decoInputExtent.reset();
        return;
    }

    QRect bounds = region.boundingRect();
    input_offset = bounds.topLeft();

    // Move the bounding rect to screen coordinates
    bounds.translate(frameGeometry().topLeft());

    // Move the region to input window coordinates
    region.translate(-input_offset);

    if (!m_decoInputExtent.isValid()) {
        auto const mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        uint32_t const values[] = {true,
            XCB_EVENT_MASK_ENTER_WINDOW   |
            XCB_EVENT_MASK_LEAVE_WINDOW   |
            XCB_EVENT_MASK_BUTTON_PRESS   |
            XCB_EVENT_MASK_BUTTON_RELEASE |
            XCB_EVENT_MASK_POINTER_MOTION
        };
        m_decoInputExtent.create(bounds, XCB_WINDOW_CLASS_INPUT_ONLY, mask, values);
        if (mapping_state == Mapped) {
            m_decoInputExtent.map();
        }
    } else {
        m_decoInputExtent.setGeometry(bounds);
    }

    auto const rects = Xcb::regionToRects(region);
    xcb_shape_rectangles(connection(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_CLIP_ORDERING_UNSORTED,
                         m_decoInputExtent, 0, 0, rects.count(), rects.constData());
}

void X11Client::updateDecoration(bool check_workspace_pos, bool force)
{
    if (!force &&
            ((!win::decoration(this) && noBorder()) || (win::decoration(this) && !noBorder()))) {
        return;
    }

    auto oldgeom = frameGeometry();
    auto oldClientGeom = oldgeom.adjusted(win::left_border(this), win::top_border(this), -win::right_border(this), -win::bottom_border(this));
    win::block_geometry_updates(this, true);

    if (force) {
        control()->destroy_decoration();
    }

    if (!noBorder()) {
        createDecoration(oldgeom);
    } else {
        control()->destroy_decoration();
    }

    win::update_shadow(this);

    if (check_workspace_pos) {
        win::check_workspace_position(this, oldgeom, -2, oldClientGeom);
    }

    updateInputWindow();
    win::block_geometry_updates(this, false);
    updateFrameExtents();
}

void X11Client::createDecoration(const QRect& oldgeom)
{
    auto decoration = Decoration::DecorationBridge::self()->createDecoration(this);

    if (decoration) {
        QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);

        connect(decoration, &KDecoration2::Decoration::shadowChanged, this,
                [this] { win::update_shadow(this); });
        connect(decoration, &KDecoration2::Decoration::resizeOnlyBordersChanged, this, &X11Client::updateInputWindow);

        connect(decoration, &KDecoration2::Decoration::bordersChanged, this,
            [this]() {
                updateFrameExtents();
                win::geometry_updates_blocker blocker(this);

                // TODO: this is obviously idempotent
                // calculateGravitation(true) would have to operate on the old border sizes
//                 move(calculateGravitation(true));
//                 move(calculateGravitation(false));

                auto oldgeom = frameGeometry();
                plainResize(sizeForClientSize(clientSize()), win::force_geometry::yes);

                if (!win::shaded(this)) {
                    win::check_workspace_position(this, oldgeom);
                }
                Q_EMIT geometryShapeChanged(this, oldgeom);
            }
        );

        connect(control()->deco().client->decoratedClient(), &KDecoration2::DecoratedClient::widthChanged, this, &X11Client::updateInputWindow);
        connect(control()->deco().client->decoratedClient(), &KDecoration2::DecoratedClient::heightChanged, this, &X11Client::updateInputWindow);
    }

    control()->deco().decoration = decoration;

    win::move(this, calculateGravitation(false));
    plainResize(sizeForClientSize(clientSize()), win::force_geometry::yes);

    if (Compositor::compositing()) {
        discardWindowPixmap();
    }
    Q_EMIT geometryShapeChanged(this, oldgeom);
}

void X11Client::layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const
{
    if (!win::decoration(this)) {
        return;
    }

    auto rect = win::decoration(this)->rect();

    top = QRect(rect.x(), rect.y(), rect.width(), win::top_border(this));
    bottom = QRect(rect.x(), rect.y() + rect.height() - win::bottom_border(this),
                   rect.width(), win::bottom_border(this));
    left = QRect(rect.x(), rect.y() + top.height(),
                 win::left_border(this), rect.height() - top.height() - bottom.height());
    right = QRect(rect.x() + rect.width() - win::right_border(this), rect.y() + top.height(),
                  win::right_border(this), rect.height() - top.height() - bottom.height());
}

void X11Client::detectNoBorder()
{
    if (shape()) {
        noborder = true;
        app_noborder = true;
        return;
    }

    switch(windowType()) {
    case NET::Desktop :
    case NET::Dock :
    case NET::TopMenu :
    case NET::Splash :
    case NET::Notification :
    case NET::OnScreenDisplay :
    case NET::CriticalNotification :
        noborder = true;
        app_noborder = true;
        break;
    case NET::Unknown :
    case NET::Normal :
    case NET::Toolbar :
    case NET::Menu :
    case NET::Dialog :
    case NET::Utility :
        noborder = false;
        break;
    default:
        abort();
    }

    // NET::Override is some strange beast without clear definition, usually
    // just meaning "noborder", so let's treat it only as such flag, and ignore it as
    // a window type otherwise (SUPPORTED_WINDOW_TYPES_MASK doesn't include it)
    if (info->windowType(NET::OverrideMask) == NET::Override) {
        noborder = true;
        app_noborder = true;
    }
}

void X11Client::updateFrameExtents()
{
    NETStrut strut;
    strut.left = win::left_border(this);
    strut.right = win::right_border(this);
    strut.top = win::top_border(this);
    strut.bottom = win::bottom_border(this);
    info->setFrameExtents(strut);
}

void X11Client::setClientFrameExtents(const NETStrut &strut)
{
    const QMargins clientFrameExtents(strut.left, strut.top, strut.right, strut.bottom);
    if (client_frame_extents == clientFrameExtents) {
        return;
    }

    client_frame_extents = clientFrameExtents;

    // We should resize the client when its custom frame extents are changed so
    // the logical bounds remain the same. This however means that we will send
    // several configure requests to the application upon restoring it from the
    // maximized or fullscreen state. Notice that a client-side decorated client
    // cannot be shaded, therefore it's okay not to use the adjusted size here.
    setFrameGeometry(frameGeometry());

    // This will invalidate the window quads cache.
    Q_EMIT geometryShapeChanged(this, frameGeometry());
}

/**
 * Resizes the decoration, and makes sure the decoration widget gets resize event
 * even if the size hasn't changed. This is needed to make sure the decoration
 * re-layouts (e.g. when maximization state changes,
 * the decoration may alter some borders, but the actual size
 * of the decoration stays the same).
 */
void X11Client::resizeDecoration()
{
    win::trigger_decoration_repaint(this);
    updateInputWindow();
}

bool X11Client::userNoBorder() const
{
    return noborder;
}

bool X11Client::noBorder() const
{
    return userNoBorder() || control()->fullscreen();
}

bool X11Client::userCanSetNoBorder() const
{
    // Client-side decorations and server-side decorations are mutually exclusive.
    if (!client_frame_extents.isNull()) {
        return false;
    }

    return !control()->fullscreen() && !win::shaded(this);
}

void X11Client::setNoBorder(bool set)
{
    if (!userCanSetNoBorder()) {
        return;
    }

    set = control()->rules().checkNoBorder(set);
    if (noborder == set) {
        return;
    }
    noborder = set;
    updateDecoration(true, false);
    updateWindowRules(Rules::NoBorder);
}

void X11Client::checkNoBorder()
{
    setNoBorder(app_noborder);
}

bool X11Client::wantsShadowToBeRendered() const
{
    return !control()->fullscreen() && maximizeMode() != win::maximize_mode::full;
}

void X11Client::updateShape()
{
    if (shape()) {
        // Workaround for #19644 - Shaped windows shouldn't have decoration
        if (!app_noborder) {
            // Only when shape is detected for the first time, still let the user to override
            app_noborder = true;
            noborder = control()->rules().checkNoBorder(true);
            updateDecoration(true);
        }
        if (noBorder()) {
            auto const client_pos = win::to_client_pos(this, QPoint());
            xcb_shape_combine(connection(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, XCB_SHAPE_SK_BOUNDING,
                              frameId(), client_pos.x(), client_pos.y(), window());
        }
    } else if (app_noborder) {
        xcb_shape_mask(connection(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, frameId(), 0, 0, XCB_PIXMAP_NONE);
        detectNoBorder();
        app_noborder = noborder;
        noborder = control()->rules().checkNoBorder(noborder || m_motif.noBorder());
        updateDecoration(true);
    }

    // Decoration mask (i.e. 'else' here) setting is done in setMask()
    // when the decoration calls it or when the decoration is created/destroyed
    updateInputShape();
    if (win::compositing()) {
        addRepaintFull();

        // In case shape change removes part of this window
        addWorkspaceRepaint(win::visible_rect(this));
    }
    Q_EMIT geometryShapeChanged(this, frameGeometry());
}

static Xcb::Window shape_helper_window(XCB_WINDOW_NONE);

void X11Client::cleanupX11()
{
    shape_helper_window.reset();
}

void X11Client::updateInputShape()
{
    if (hiddenPreview()) {
        // Sets it to none, don't change
        return;
    }

    if (Xcb::Extensions::self()->isShapeInputAvailable()) {
        // There appears to be no way to find out if a window has input
        // shape set or not, so always propagate the input shape
        // (it's the same like the bounding shape by default).
        // Also, build the shape using a helper window, not directly
        // in the frame window, because the sequence set-shape-to-frame,
        // remove-shape-of-client, add-input-shape-of-client has the problem
        // that after the second step there's a hole in the input shape
        // until the real shape of the client is added and that can make
        // the window lose focus (which is a problem with mouse focus policies)
        // TODO: It seems there is, after all - XShapeGetRectangles() - but maybe this is better
        if (!shape_helper_window.isValid()) {
            shape_helper_window.create(QRect(0, 0, 1, 1));
        }

        shape_helper_window.resize(m_bufferGeometry.size());
        auto c = connection();
        auto const client_pos = win::to_client_pos(this, QPoint());

        xcb_shape_combine(c, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_SHAPE_SK_BOUNDING,
                          shape_helper_window, 0, 0, frameId());
        xcb_shape_combine(c, XCB_SHAPE_SO_SUBTRACT, XCB_SHAPE_SK_INPUT, XCB_SHAPE_SK_BOUNDING,
                          shape_helper_window, client_pos.x(), client_pos.y(), window());
        xcb_shape_combine(c, XCB_SHAPE_SO_UNION, XCB_SHAPE_SK_INPUT, XCB_SHAPE_SK_INPUT,
                          shape_helper_window, client_pos.x(), client_pos.y(), window());
        xcb_shape_combine(c, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT, XCB_SHAPE_SK_INPUT,
                          frameId(), 0, 0, shape_helper_window);
    }
}

void X11Client::hideClient(bool hide)
{
    if (hidden == hide) {
        return;
    }
    hidden = hide;
    updateVisibility();
}

bool X11Client::setupCompositing(bool add_full_damage)
{
    if (!Toplevel::setupCompositing(add_full_damage)){
        return false;
    }

    // for internalKeep()
    updateVisibility();

    return true;
}

void X11Client::finishCompositing(ReleaseReason releaseReason)
{
    Toplevel::finishCompositing(releaseReason);
    updateVisibility();

    // for safety in case KWin is just resizing the window
    control()->reset_have_resize_effect();
}

/**
 * Returns whether the window is minimizable or not
 */
bool X11Client::isMinimizable() const
{
    if (win::is_special_window(this) && !isTransient()) {
        return false;
    }
    if (!control()->rules().checkMinimize(true)) {
        return false;
    }

    if (isTransient()) {
        // #66868 - Let other xmms windows be minimized when the mainwindow is minimized
        bool shown_mainwindow = false;
        for (auto const& lead : transient()->leads())
            if (lead->isShown(true)) {
                shown_mainwindow = true;
            }
        if (!shown_mainwindow) {
            return true;
        }
    }

#if 0
    // This is here because kicker's taskbar doesn't provide separate entries
    // for windows with an explicitly given parent
    // TODO: perhaps this should be redone
    // Disabled for now, since at least modal dialogs should be minimizable
    // (resulting in the mainwindow being minimized too).
    if (transientFor() != NULL)
        return false;
#endif

    if (!win::wants_tab_focus(this)) {
        // SELI, TODO: - NET::Utility? why wantsTabFocus() - skiptaskbar? ?
        return false;
    }
    return true;
}

void X11Client::doMinimize()
{
    updateVisibility();
    updateAllowedActions();
    workspace()->updateMinimizedOfTransients(this);
}

QRect X11Client::iconGeometry() const
{
    auto rect = info->iconGeometry();

    QRect geom(rect.pos.x, rect.pos.y, rect.size.width, rect.size.height);
    if (geom.isValid()) {
        return geom;

    }

    // Check all mainwindows of this window (recursively)
    for (auto mc : transient()->leads()) {
        geom = mc->iconGeometry();
        if (geom.isValid()) {
            return geom;
        }
    }

    // No mainwindow (or their parents) with icon geometry was found
    return Toplevel::iconGeometry();
}

bool X11Client::isShadeable() const
{
    return !win::is_special_window(this) && !noBorder() && (control()->rules().checkShade(win::shade::normal) != control()->rules().checkShade(win::shade::none));
}

void X11Client::setShade(win::shade mode)
{
    if (mode == win::shade::hover && win::is_move(this)) {
        // causes geometry breaks and is probably nasty
        return;
    }

    if (win::is_special_window(this) || noBorder()) {
        mode = win::shade::none;
    }

    mode = control()->rules().checkShade(mode);

    if (shade_mode == mode) {
        return;
    }

    auto was_shade = win::shaded(this);
    auto was_shade_mode = shade_mode;
    shade_mode = mode;

    // Decorations may turn off some borders when shaded
    // this has to happen _before_ the tab alignment since it will restrict the minimum geometry
#if 0
    if (decoration)
        decoration->borders(border_left, border_right, border_top, border_bottom);
#endif

    if (was_shade == win::shaded(this)) {
        // Decoration may want to update after e.g. hover-shade changes
        Q_EMIT shadeChanged();

        // No real change in shaded state
        return;
    }

    // noborder windows can't be shaded
    assert(win::decoration(this));

    win::geometry_updates_blocker blocker(this);

    // TODO: All this unmapping, resizing etc. feels too much duplicated from elsewhere
    if (win::shaded(this)) {
        // shade_mode == win::shade::normal
        addWorkspaceRepaint(win::visible_rect(this));

        // Shade
        shade_geometry_change = true;
        QSize s(sizeForClientSize(QSize(clientSize())));
        s.setHeight(win::top_border(this) + win::bottom_border(this));

        // Avoid getting UnmapNotify
        m_wrapper.selectInput(ClientWinMask);

        m_wrapper.unmap();
        m_client.unmap();

        m_wrapper.selectInput(ClientWinMask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
        exportMappingState(XCB_ICCCM_WM_STATE_ICONIC);
        plainResize(s);
        shade_geometry_change = false;

        if (was_shade_mode == win::shade::hover) {
            if (shade_below && index_of(workspace()->stackingOrder(), shade_below) > -1) {
                    workspace()->restack(this, shade_below, true);
            }
            if (control()->active()) {
                workspace()->activateNextClient(this);
            }
        } else if (control()->active()) {
            workspace()->focusToNull();
        }
    } else {
        shade_geometry_change = true;
        if (auto deco_client = control()->deco().client) {
            deco_client->signalShadeChange();
        }

        QSize s(sizeForClientSize(clientSize()));
        shade_geometry_change = false;

        plainResize(s);
        restore_geometries.shade = frameGeometry();

        if ((shade_mode == win::shade::hover || shade_mode == win::shade::activated)
                && control()->rules().checkAcceptFocus(info->input())) {
            win::set_active(this, true);
        }

        if (shade_mode == win::shade::hover) {
            auto order = workspace()->stackingOrder();
            // invalidate, since "this" could be the topmost toplevel and shade_below dangeling
            shade_below = nullptr;
            // this is likely related to the index parameter?!
            for (size_t idx = index_of(order, this) + 1; idx < order.size(); ++idx) {
                shade_below = qobject_cast<X11Client *>(order.at(idx));
                if (shade_below) {
                    break;
                }
            }

            if (shade_below && win::is_normal(shade_below)) {
                workspace()->raise_window(this);
            }else {
                shade_below = nullptr;
            }
        }

        m_wrapper.map();
        m_client.map();

        exportMappingState(XCB_ICCCM_WM_STATE_NORMAL);
        if (control()->active()) {
            workspace()->request_focus(this);
        }
    }

    info->setState(win::shaded(this) ? NET::Shaded : NET::States(), NET::Shaded);
    info->setState(isShown(false) ? NET::States() : NET::Hidden, NET::Hidden);

    discardWindowPixmap();
    updateVisibility();
    updateAllowedActions();
    updateWindowRules(Rules::Shade);

    Q_EMIT shadeChanged();
}

void X11Client::shadeHover()
{
    setShade(win::shade::hover);
    cancelShadeHoverTimer();
}

void X11Client::shadeUnhover()
{
    setShade(win::shade::normal);
    cancelShadeHoverTimer();
}

void X11Client::cancelShadeHoverTimer()
{
    delete shadeHoverTimer;
    shadeHoverTimer = nullptr;
}

void X11Client::toggleShade()
{
    // If the mode is win::shade::hover or win::shade::active, cancel shade too
    setShade(shade_mode == win::shade::none ? win::shade::normal : win::shade::none);
}

void X11Client::updateVisibility()
{
    if (deleting) {
        return;
    }

    if (hidden) {
        info->setState(NET::Hidden, NET::Hidden);
        win::set_skip_taskbar(this, true);   // Also hide from taskbar
        if (win::compositing() && options->hiddenPreviews() == HiddenPreviewsAlways) {
            internalKeep();
        } else {
            internalHide();
        }
        return;
    }

    win::set_skip_taskbar(this, control()->original_skip_taskbar());   // Reset from 'hidden'
    if (control()->minimized()) {
        info->setState(NET::Hidden, NET::Hidden);
        if (win::compositing() && options->hiddenPreviews() == HiddenPreviewsAlways) {
            internalKeep();
        } else {
            internalHide();
        }
        return;
    }

    info->setState(NET::States(), NET::Hidden);
    if (!isOnCurrentDesktop()) {
        if (win::compositing() && options->hiddenPreviews() != HiddenPreviewsNever) {
            internalKeep();
        } else {
            internalHide();
        }
        return;
    }
    if (!isOnCurrentActivity()) {
        if (win::compositing() && options->hiddenPreviews() != HiddenPreviewsNever) {
            internalKeep();
        } else {
            internalHide();
        }
        return;
    }
    internalShow();
}


/**
 * Sets the client window's mapping state. Possible values are
 * WithdrawnState, IconicState, NormalState.
 */
void X11Client::exportMappingState(int s)
{
    assert(m_client != XCB_WINDOW_NONE);
    assert(!deleting || s == XCB_ICCCM_WM_STATE_WITHDRAWN);

    if (s == XCB_ICCCM_WM_STATE_WITHDRAWN) {
        m_client.deleteProperty(atoms->wm_state);
        return;
    }

    assert(s == XCB_ICCCM_WM_STATE_NORMAL || s == XCB_ICCCM_WM_STATE_ICONIC);

    int32_t data[2];
    data[0] = s;
    data[1] = XCB_NONE;
    m_client.changeProperty(atoms->wm_state, atoms->wm_state, 32, 2, data);
}

void X11Client::internalShow()
{
    if (mapping_state == Mapped) {
        return;
    }

    MappingState old = mapping_state;
    mapping_state = Mapped;

    if (old == Unmapped || old == Withdrawn) {
        map();
    }

    if (old == Kept) {
        m_decoInputExtent.map();
        updateHiddenPreview();
    }

    Q_EMIT windowShown(this);
}

void X11Client::internalHide()
{
    if (mapping_state == Unmapped) {
        return;
    }

    MappingState old = mapping_state;
    mapping_state = Unmapped;

    if (old == Mapped || old == Kept) {
        unmap();
    }
    if (old == Kept) {
        updateHiddenPreview();
    }

    addWorkspaceRepaint(win::visible_rect(this));
    workspace()->clientHidden(this);
    Q_EMIT windowHidden(this);
}

void X11Client::internalKeep()
{
    assert(win::compositing());

    if (mapping_state == Kept) {
        return;
    }

    auto old = mapping_state;
    mapping_state = Kept;

    if (old == Unmapped || old == Withdrawn) {
        map();
    }

    m_decoInputExtent.unmap();
    if (control()->active()) {
        // get rid of input focus, bug #317484
        workspace()->focusToNull();
    }

    updateHiddenPreview();
    addWorkspaceRepaint(win::visible_rect(this));
    workspace()->clientHidden(this);
}

/**
 * Maps (shows) the client. Note that it is mapping state of the frame,
 * not necessarily the client window itself (i.e. a shaded window is here
 * considered mapped, even though it is in IconicState).
 */
void X11Client::map()
{
    // XComposite invalidates backing pixmaps on unmap (minimize, different
    // virtual desktop, etc.).  We kept the last known good pixmap around
    // for use in effects, but now we want to have access to the new pixmap
    if (win::compositing()) {
        discardWindowPixmap();
    }

    m_frame.map();
    if (!win::shaded(this)) {
        m_wrapper.map();
        m_client.map();
        m_decoInputExtent.map();
        exportMappingState(XCB_ICCCM_WM_STATE_NORMAL);
    } else {
        exportMappingState(XCB_ICCCM_WM_STATE_ICONIC);
    }

    addLayerRepaint(win::visible_rect(this));
}

/**
 * Unmaps the client. Again, this is about the frame.
 */
void X11Client::unmap()
{
    // Here it may look like a race condition, as some other client might try to unmap
    // the window between these two XSelectInput() calls. However, they're supposed to
    // use XWithdrawWindow(), which also sends a synthetic event to the root window,
    // which won't be missed, so this shouldn't be a problem. The chance the real UnmapNotify
    // will be missed is also very minimal, so I don't think it's needed to grab the server
    // here.
    m_wrapper.selectInput(ClientWinMask);   // Avoid getting UnmapNotify
    m_frame.unmap();
    m_wrapper.unmap();
    m_client.unmap();
    m_decoInputExtent.unmap();
    m_wrapper.selectInput(ClientWinMask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
    exportMappingState(XCB_ICCCM_WM_STATE_ICONIC);
}

/**
 * XComposite doesn't keep window pixmaps of unmapped windows, which means
 * there wouldn't be any previews of windows that are minimized or on another
 * virtual desktop. Therefore rawHide() actually keeps such windows mapped.
 * However special care needs to be taken so that such windows don't interfere.
 * Therefore they're put very low in the stacking order and they have input shape
 * set to none, which hopefully is enough. If there's no input shape available,
 * then it's hoped that there will be some other desktop above it *shrug*.
 * Using normal shape would be better, but that'd affect other things, e.g. painting
 * of the actual preview.
 */
void X11Client::updateHiddenPreview()
{
    if (hiddenPreview()) {
        workspace()->forceRestacking();
        if (Xcb::Extensions::self()->isShapeInputAvailable()) {
            xcb_shape_rectangles(connection(), XCB_SHAPE_SO_SET, XCB_SHAPE_SK_INPUT,
                                 XCB_CLIP_ORDERING_UNSORTED, frameId(), 0, 0, 0, nullptr);
        }
    } else {
        workspace()->forceRestacking();
        updateInputShape();
    }
}

void X11Client::sendClientMessage(xcb_window_t w, xcb_atom_t a, xcb_atom_t protocol, uint32_t data1, uint32_t data2, uint32_t data3)
{
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = w;
    ev.type = a;
    ev.format = 32;
    ev.data.data32[0] = protocol;
    ev.data.data32[1] = xTime();
    ev.data.data32[2] = data1;
    ev.data.data32[3] = data2;
    ev.data.data32[4] = data3;
    uint32_t eventMask = 0;

    if (w == rootWindow()) {
        // Magic!
        eventMask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
    }

    xcb_send_event(connection(), false, w, eventMask, reinterpret_cast<const char*>(&ev));
    xcb_flush(connection());
}

/**
 * Returns whether the window may be closed (have a close button)
 */
bool X11Client::isCloseable() const
{
    return control()->rules().checkCloseable(m_motif.close() && !win::is_special_window(this));
}

/**
 * Closes the window by either sending a delete_window message or using XKill.
 */
void X11Client::closeWindow()
{
    if (!isCloseable()) {
        return;
    }

    // Update user time, because the window may create a confirming dialog.
    updateUserTime();

    if (info->supportsProtocol(NET::DeleteWindowProtocol)) {
        sendClientMessage(window(), atoms->wm_protocols, atoms->wm_delete_window);
        pingWindow();
    } else {
        // Client will not react on wm_delete_window. We have not choice
        // but destroy his connection to the XServer.
        killWindow();
    }
}


/**
 * Kills the window via XKill
 */
void X11Client::killWindow()
{
    qCDebug(KWIN_CORE) << "X11Client::killWindow():" << win::caption(this);
    killProcess(false);

    // Always kill this client at the server
    m_client.kill();

    destroyClient();
}

/**
 * Send a ping to the window using _NET_WM_PING if possible if it
 * doesn't respond within a reasonable time, it will be killed.
 */
void X11Client::pingWindow()
{
    if (!info->supportsProtocol(NET::PingProtocol)) {
        // Can't ping :(
        return;
    }
    if (options->killPingTimeout() == 0) {
        // Turned off
        return;
    }
    if (ping_timer != nullptr) {
        // Pinging already
        return;
    }

    ping_timer = new QTimer(this);

    connect(ping_timer, &QTimer::timeout, this,
        [this]() {
            if (control()->unresponsive()) {
                qCDebug(KWIN_CORE) << "Final ping timeout, asking to kill:" << win::caption(this);
                ping_timer->deleteLater();
                ping_timer = nullptr;
                killProcess(true, m_pingTimestamp);
                return;
            }

            qCDebug(KWIN_CORE) << "First ping timeout:" << win::caption(this);

            control()->set_unresponsive(true);
            ping_timer->start();
        }
    );

    ping_timer->setSingleShot(true);

    // We'll run the timer twice, at first we'll desaturate the window
    // and the second time we'll show the "do you want to kill" prompt.
    ping_timer->start(options->killPingTimeout() / 2);

    m_pingTimestamp = xTime();
    rootInfo()->sendPing(window(), m_pingTimestamp);
}

void X11Client::gotPing(xcb_timestamp_t timestamp)
{
    // Just plain compare is not good enough because of 64bit and truncating and whatnot
    if (NET::timestampCompare(timestamp, m_pingTimestamp) != 0) {
        return;
    }

    delete ping_timer;
    ping_timer = nullptr;

    control()->set_unresponsive(false);

    if (m_killHelperPID && !::kill(m_killHelperPID, 0)) { // means the process is alive
        ::kill(m_killHelperPID, SIGTERM);
        m_killHelperPID = 0;
    }
}

void X11Client::killProcess(bool ask, xcb_timestamp_t timestamp)
{
    if (m_killHelperPID && !::kill(m_killHelperPID, 0)) {
        // means the process is alive
        return;
    }

    assert(!ask || timestamp != XCB_TIME_CURRENT_TIME);

    auto pid = info->pid();
    if (pid <= 0 || clientMachine()->hostName().isEmpty()) {
        // Needed properties missing
        return;
    }

    qCDebug(KWIN_CORE) << "Kill process:" << pid << "(" << clientMachine()->hostName() << ")";

    if (!ask) {
        if (!clientMachine()->isLocal()) {
            QStringList lst;
            lst << QString::fromUtf8(clientMachine()->hostName()) << QStringLiteral("kill") << QString::number(pid);
            QProcess::startDetached(QStringLiteral("xon"), lst);
        } else {
            ::kill(pid, SIGTERM);
        }
    } else {
        auto hostname = clientMachine()->isLocal() ? QStringLiteral("localhost") : QString::fromUtf8(clientMachine()->hostName());
        // execute helper from build dir or the system installed one
        QFileInfo const buildDirBinary{QDir{QCoreApplication::applicationDirPath()}, QStringLiteral("kwin_killer_helper")};
        QProcess::startDetached(buildDirBinary.exists() ? buildDirBinary.absoluteFilePath() : QStringLiteral(KWIN_KILLER_BIN),
                                QStringList() << QStringLiteral("--pid") << QString::number(unsigned(pid)) << QStringLiteral("--hostname") << hostname
                                << QStringLiteral("--windowname") << captionNormal()
                                << QStringLiteral("--applicationname") << QString::fromUtf8(resourceClass())
                                << QStringLiteral("--wid") << QString::number(window())
                                << QStringLiteral("--timestamp") << QString::number(timestamp),
                                QString(), &m_killHelperPID);
    }
}

void X11Client::doSetDesktop(int desktop, int was_desk)
{
    Q_UNUSED(desktop)
    Q_UNUSED(was_desk)
    updateVisibility();
}

/**
 * Sets whether the client is on @p activity.
 * If you remove it from its last activity, then it's on all activities.
 *
 * Note: If it was on all activities and you try to remove it from one, nothing will happen;
 * I don't think that's an important enough use case to handle here.
 */
void X11Client::setOnActivity(const QString &activity, bool enable)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return;
    }
    auto newActivitiesList = activities();
    if (newActivitiesList.contains(activity) == enable) {
        //nothing to do
        return;
    }
    if (enable) {
        QStringList allActivities = Activities::self()->all();
        if (!allActivities.contains(activity)) {
            //bogus ID
            return;
        }
        newActivitiesList.append(activity);
    } else
        newActivitiesList.removeOne(activity);
    setOnActivities(newActivitiesList);
#else
    Q_UNUSED(activity)
    Q_UNUSED(enable)
#endif
}

/**
 * set exactly which activities this client is on
 */
void X11Client::setOnActivities(QStringList newActivitiesList)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return;
    }
    QString joinedActivitiesList = newActivitiesList.join(QStringLiteral(","));
    joinedActivitiesList = control()->rules().checkActivity(joinedActivitiesList, false);
    newActivitiesList = joinedActivitiesList.split(u',', Qt::SkipEmptyParts);

    QStringList allActivities = Activities::self()->all();

    auto it = newActivitiesList.begin();
    while (it != newActivitiesList.end()) {
        if (! allActivities.contains(*it)) {
            it = newActivitiesList.erase(it);
        } else {
            it++;
        }
    }

    if (// If we got the request to be on all activities explicitly
        newActivitiesList.isEmpty() || joinedActivitiesList == Activities::nullUuid() ||
        // If we got a list of activities that covers all activities
        (newActivitiesList.count() > 1 && newActivitiesList.count() == allActivities.count())) {

        activityList.clear();
        const QByteArray nullUuid = Activities::nullUuid().toUtf8();
        m_client.changeProperty(atoms->activities, XCB_ATOM_STRING, 8, nullUuid.length(), nullUuid.constData());

    } else {
        QByteArray joined = joinedActivitiesList.toLatin1();
        activityList = newActivitiesList;
        m_client.changeProperty(atoms->activities, XCB_ATOM_STRING, 8, joined.length(), joined.constData());
    }

    updateActivities(false);
#else
    Q_UNUSED(newActivitiesList)
#endif
}

void X11Client::blockActivityUpdates(bool b)
{
    if (b) {
        ++m_activityUpdatesBlocked;
    } else {
        Q_ASSERT(m_activityUpdatesBlocked);
        --m_activityUpdatesBlocked;
        if (!m_activityUpdatesBlocked)
            updateActivities(m_blockedActivityUpdatesRequireTransients);
    }
}

/**
 * update after activities changed
 */
void X11Client::updateActivities(bool includeTransients)
{
    if (m_activityUpdatesBlocked) {
        m_blockedActivityUpdatesRequireTransients |= includeTransients;
        return;
    }

    Q_EMIT activitiesChanged(this);

    // reset
    m_blockedActivityUpdatesRequireTransients = false;

    FocusChain::self()->update(this, FocusChain::MakeFirst);
    updateVisibility();
    updateWindowRules(Rules::Activity);
}

/**
 * Returns the list of activities the client window is on.
 * if it's on all activities, the list will be empty.
 * Don't use this, use isOnActivity() and friends (from class Toplevel)
 */
QStringList X11Client::activities() const
{
    if (sessionActivityOverride) {
        return QStringList();
    }
    return activityList;
}

/**
 * if @p on is true, sets on all activities.
 * if it's false, sets it to only be on the current activity
 */
void X11Client::setOnAllActivities(bool on)
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (on == isOnAllActivities()) {
        return;
    }
    if (on) {
        setOnActivities(QStringList());

    } else {
        setOnActivity(Activities::self()->current(), true);
    }
#else
    Q_UNUSED(on)
#endif
}

/**
 * Performs the actual focusing of the window using XSetInputFocus and WM_TAKE_FOCUS
 */
void X11Client::takeFocus()
{
    if (control()->rules().checkAcceptFocus(info->input())) {
        m_client.focus();
    } else {
        // window cannot take input, at least withdraw urgency
        win::set_demands_attention(this, false);
    }

    if (info->supportsProtocol(NET::TakeFocusProtocol)) {
        updateXTime();
        sendClientMessage(window(), atoms->wm_protocols, atoms->wm_take_focus);
    }

    workspace()->setShouldGetFocus(this);
    auto breakShowingDesktop = !control()->keep_above();

    if (breakShowingDesktop) {
        foreach (const X11Client *c, group()->members()) {
            if (win::is_desktop(c)) {
                breakShowingDesktop = false;
                break;
            }
        }
    }

    if (breakShowingDesktop) {
        workspace()->setShowingDesktop(false);
    }
}

/**
 * Returns whether the window provides context help or not. If it does,
 * you should show a help menu item or a help button like '?' and call
 * contextHelp() if this is invoked.
 *
 * \sa contextHelp()
 */
bool X11Client::providesContextHelp() const
{
    return info->supportsProtocol(NET::ContextHelpProtocol);
}

/**
 * Invokes context help on the window. Only works if the window
 * actually provides context help.
 *
 * \sa providesContextHelp()
 */
void X11Client::showContextHelp()
{
    if (info->supportsProtocol(NET::ContextHelpProtocol)) {
        sendClientMessage(window(), atoms->wm_protocols, atoms->net_wm_context_help);
    }
}

/**
 * Fetches the window's caption (WM_NAME property). It will be
 * stored in the client's caption().
 */
void X11Client::fetchName()
{
    setCaption(readName());
}

static inline QString readNameProperty(xcb_window_t w, xcb_atom_t atom)
{
    auto const cookie = xcb_icccm_get_text_property_unchecked(connection(), w, atom);
    xcb_icccm_get_text_property_reply_t reply;

    if (xcb_icccm_get_wm_name_reply(connection(), cookie, &reply, nullptr)) {
        QString retVal;
        if (reply.encoding == atoms->utf8_string) {
            retVal = QString::fromUtf8(QByteArray(reply.name, reply.name_len));
        } else if (reply.encoding == XCB_ATOM_STRING) {
            retVal = QString::fromLocal8Bit(QByteArray(reply.name, reply.name_len));
        }
        xcb_icccm_get_text_property_reply_wipe(&reply);
        return retVal.simplified();
    }

    return QString();
}

QString X11Client::readName() const
{
    if (info->name() && info->name()[0] != '\0') {
        return QString::fromUtf8(info->name()).simplified();
    }

    return readNameProperty(window(), XCB_ATOM_WM_NAME);
}

// The list is taken from https://www.unicode.org/reports/tr9/ (#154840)
static const QChar LRM(0x200E);

void X11Client::setCaption(const QString& _s, bool force)
{
    QString s(_s);
    for (int i = 0; i < s.length(); ) {

        if (!s[i].isPrint()) {

            if (QChar(s[i]).isHighSurrogate() && i + 1 < s.length() && QChar(s[i + 1]).isLowSurrogate()) {
                const uint uc = QChar::surrogateToUcs4(s[i], s[i + 1]);

                if (!QChar::isPrint(uc)) {
                    s.remove(i, 2);
                } else {
                    i += 2;
                }
                continue;
            }
            s.remove(i, 1);
            continue;
        }

        ++i;
    }

    auto const changed = (s != cap_normal);
    if (!force && !changed) {
        return;
    }

    cap_normal = s;

    if (!force && !changed) {
        Q_EMIT captionChanged();
        return;
    }

    auto reset_name = force;
    auto was_suffix = (!cap_suffix.isEmpty());
    cap_suffix.clear();

    QString machine_suffix;
    if (!options->condensedTitle()) {
        // machine doesn't qualify for "clean"
        if (clientMachine()->hostName() != ClientMachine::localhost() && !clientMachine()->isLocal()) {
            machine_suffix = QLatin1String(" <@") + QString::fromUtf8(clientMachine()->hostName()) + QLatin1Char('>') + LRM;
        }
    }
    auto shortcut_suffix = win::shortcut_caption_suffix(this);
    cap_suffix = machine_suffix + shortcut_suffix;

    if ((!win::is_special_window(this) || win::is_toolbar(this))
            && win::find_client_with_same_caption(static_cast<Toplevel*>(this))) {
        int i = 2;

        do {
            cap_suffix = machine_suffix + QLatin1String(" <") + QString::number(i) + QLatin1Char('>') + LRM;
            i++;
        } while (win::find_client_with_same_caption(static_cast<Toplevel*>(this)));

        info->setVisibleName(win::caption(this).toUtf8().constData());
        reset_name = false;
    }

    if ((was_suffix && cap_suffix.isEmpty()) || reset_name) {
        // If it was new window, it may have old value still set, if the window is reused
        info->setVisibleName("");
        info->setVisibleIconName("");
    } else if (!cap_suffix.isEmpty() && !cap_iconic.isEmpty()) {
        // Keep the same suffix in iconic name if it's set
        info->setVisibleIconName(QString(cap_iconic + cap_suffix).toUtf8().constData());
    }

    Q_EMIT captionChanged();
}

void X11Client::updateCaption()
{
    setCaption(cap_normal, true);
}

void X11Client::fetchIconicName()
{
    QString s;
    if (info->iconName() && info->iconName()[0] != '\0') {
        s = QString::fromUtf8(info->iconName());
    } else {
        s = readNameProperty(window(), XCB_ATOM_WM_ICON_NAME);
    }

    if (s != cap_iconic) {
        bool was_set = !cap_iconic.isEmpty();
        cap_iconic = s;
        if (!cap_suffix.isEmpty()) {
            if (!cap_iconic.isEmpty()) {
                // Keep the same suffix in iconic name if it's set
                info->setVisibleIconName(QString(s + cap_suffix).toUtf8().constData());
            } else if (was_set) {
                info->setVisibleIconName("");
            }
        }
    }
}

void X11Client::setClientShown(bool shown)
{
    if (deleting) {
        // Don't change shown status if this client is being deleted
        return;
    }
    if (shown != hidden) {
        // nothing to change
        return;
    }

    hidden = !shown;

    if (shown) {
        map();
        takeFocus();
        win::auto_raise(this);
        FocusChain::self()->update(this, FocusChain::MakeFirst);
    } else {
        unmap();
        // Don't move tabs to the end of the list when another tab get's activated
        FocusChain::self()->update(this, FocusChain::MakeLast);
        addWorkspaceRepaint(win::visible_rect(this));
    }
}

void X11Client::getMotifHints()
{
    auto const wasClosable = m_motif.close();
    auto const wasNoBorder = m_motif.noBorder();

    if (m_managed) {
        // only on property change, initial read is prefetched
        m_motif.fetch();
    }

    m_motif.read();

    if (m_motif.hasDecoration() && m_motif.noBorder() != wasNoBorder) {
        // If we just got a hint telling us to hide decorations, we do so but only do so if the app
        // didn't instruct us to hide decorations in some other way.
        if (m_motif.noBorder()) {
            noborder = control()->rules().checkNoBorder(true);
        } else if (!app_noborder) {
            noborder = control()->rules().checkNoBorder(false);
        }
    }

    // mminimize; - Ignore, bogus - E.g. shading or sending to another desktop is "minimizing" too
    // mmaximize; - Ignore, bogus - Maximizing is basically just resizing

    auto const closabilityChanged = wasClosable != m_motif.close();
    if (isManaged()) {
        // Check if noborder state has changed
        updateDecoration(true);
    }
    if (closabilityChanged) {
        Q_EMIT closeableChanged(isCloseable());
    }
}

void X11Client::getIcons()
{
    // First read icons from the window itself
    auto const themedIconName = win::icon_from_desktop_file(this);
    if (!themedIconName.isEmpty()) {
        control()->set_icon(QIcon::fromTheme(themedIconName));
        return;
    }

    QIcon icon;
    auto readIcon = [this, &icon](int size, bool scale = true) {
        const QPixmap pix = KWindowSystem::icon(window(), size, size, scale, KWindowSystem::NETWM | KWindowSystem::WMHints, info);
        if (!pix.isNull()) {
            icon.addPixmap(pix);
        }
    };

    readIcon(16);
    readIcon(32);
    readIcon(48, false);
    readIcon(64, false);
    readIcon(128, false);

    if (icon.isNull()) {
        // Then try window group
        icon = group()->icon();
    }

    if (icon.isNull()) {
        for (auto lead : transient()->leads()) {
            if (!lead->control()->icon().isNull()) {
                icon = lead->control()->icon();
                break;
            }
        }
    }
    if (icon.isNull()) {
        // And if nothing else, load icon from classhint or xapp icon
        icon.addPixmap(KWindowSystem::icon(window(),  32,  32,  true, KWindowSystem::ClassHint | KWindowSystem::XApp, info));
        icon.addPixmap(KWindowSystem::icon(window(),  16,  16,  true, KWindowSystem::ClassHint | KWindowSystem::XApp, info));
        icon.addPixmap(KWindowSystem::icon(window(),  64,  64, false, KWindowSystem::ClassHint | KWindowSystem::XApp, info));
        icon.addPixmap(KWindowSystem::icon(window(), 128, 128, false, KWindowSystem::ClassHint | KWindowSystem::XApp, info));
    }
    control()->set_icon(icon);
}

/**
 * Returns \c true if X11Client wants to throttle resizes; otherwise returns \c false.
 */
bool X11Client::wantsSyncCounter() const
{
    return true;
}

void X11Client::getSyncCounter()
{
    if (!Xcb::Extensions::self()->isSyncAvailable()) {
        return;
    }
    if (!wantsSyncCounter()) {
        return;
    }

    Xcb::Property syncProp(false, window(), atoms->net_wm_sync_request_counter, XCB_ATOM_CARDINAL, 0, 1);
    auto const counter = syncProp.value<xcb_sync_counter_t>(XCB_NONE);

    if (counter != XCB_NONE) {
        m_syncRequest.counter = counter;
        m_syncRequest.value.hi = 0;
        m_syncRequest.value.lo = 0;

        auto con = connection();
        xcb_sync_set_counter(con, m_syncRequest.counter, m_syncRequest.value);

        if (m_syncRequest.alarm == XCB_NONE) {
            const uint32_t mask = XCB_SYNC_CA_COUNTER | XCB_SYNC_CA_VALUE_TYPE | XCB_SYNC_CA_TEST_TYPE | XCB_SYNC_CA_EVENTS;
            const uint32_t values[] = {
                m_syncRequest.counter,
                XCB_SYNC_VALUETYPE_RELATIVE,
                XCB_SYNC_TESTTYPE_POSITIVE_TRANSITION,
                1
            };

            m_syncRequest.alarm = xcb_generate_id(con);
            auto cookie = xcb_sync_create_alarm_checked(con, m_syncRequest.alarm, mask, values);
            ScopedCPointer<xcb_generic_error_t> error(xcb_request_check(con, cookie));

            if (!error.isNull()) {
                m_syncRequest.alarm = XCB_NONE;
            } else {
                xcb_sync_change_alarm_value_list_t value;
                memset(&value, 0, sizeof(value));
                value.value.hi = 0;
                value.value.lo = 1;
                value.delta.hi = 0;
                value.delta.lo = 1;
                xcb_sync_change_alarm_aux(con, m_syncRequest.alarm, XCB_SYNC_CA_DELTA | XCB_SYNC_CA_VALUE, &value);
            }
        }
    }
}

/**
 * Send the client a _NET_SYNC_REQUEST
 */
void X11Client::sendSyncRequest()
{
    if (m_syncRequest.counter == XCB_NONE || m_syncRequest.isPending) {
        // do NOT, NEVER send a sync request when there's one on the stack. the clients will just stop respoding. FOREVER! ...
        return;
    }

    if (!m_syncRequest.failsafeTimeout) {
        m_syncRequest.failsafeTimeout = new QTimer(this);

        connect(m_syncRequest.failsafeTimeout, &QTimer::timeout, this,
            [this]() {
                // client does not respond to XSYNC requests in reasonable time, remove support
                if (!ready_for_painting) {
                    // failed on initial pre-show request
                    setReadyForPainting();
                    win::setup_wayland_plasma_management(this);
                    return;
                }
                // failed during resize
                m_syncRequest.isPending = false;
                m_syncRequest.counter = XCB_NONE;
                m_syncRequest.alarm = XCB_NONE;
                delete m_syncRequest.timeout;
                delete m_syncRequest.failsafeTimeout;
                m_syncRequest.timeout = nullptr;
                m_syncRequest.failsafeTimeout = nullptr;
                m_syncRequest.lastTimestamp = XCB_CURRENT_TIME;
            }
        );

        m_syncRequest.failsafeTimeout->setSingleShot(true);
    }

    // If there's no response within 10 seconds, sth. went wrong and we remove XSYNC support from this client.
    // see events.cpp X11Client::syncEvent()
    m_syncRequest.failsafeTimeout->start(ready_for_painting ? 10000 : 1000);

    // We increment before the notify so that after the notify
    // syncCounterSerial will equal the value we are expecting
    // in the acknowledgement
    auto const oldLo = m_syncRequest.value.lo;
    m_syncRequest.value.lo++;

    if (oldLo > m_syncRequest.value.lo) {
        m_syncRequest.value.hi++;
    }
    if (m_syncRequest.lastTimestamp >= xTime()) {
        updateXTime();
    }

    // Send the message to client
    sendClientMessage(window(), atoms->wm_protocols, atoms->net_wm_sync_request,
                      m_syncRequest.value.lo, m_syncRequest.value.hi);
    m_syncRequest.isPending = true;
    m_syncRequest.lastTimestamp = xTime();
}

bool X11Client::wantsInput() const
{
    return control()->rules().checkAcceptFocus(acceptsFocus() || info->supportsProtocol(NET::TakeFocusProtocol));
}

bool X11Client::acceptsFocus() const
{
    return info->input();
}

void X11Client::setBlockingCompositing(bool block)
{
    auto const usedToBlock = blocks_compositing;
    blocks_compositing = control()->rules().checkBlockCompositing(block && options->windowsBlockCompositing());

    if (usedToBlock != blocks_compositing) {
        Q_EMIT blockingCompositingChanged(blocks_compositing ? this : nullptr);
    }
}

void X11Client::updateAllowedActions(bool force)
{
    if (!isManaged() && !force) {
        return;
    }

    auto old_allowed_actions = NET::Actions(allowed_actions);
    allowed_actions = NET::Actions();

    if (isMovable()) {
        allowed_actions |= NET::ActionMove;
    }
    if (isResizable()) {
        allowed_actions |= NET::ActionResize;
    }
    if (isMinimizable()) {
        allowed_actions |= NET::ActionMinimize;
    }
    if (isShadeable()) {
        allowed_actions |= NET::ActionShade;
    }

    // Sticky state not supported
    if (isMaximizable()) {
        allowed_actions |= NET::ActionMax;
    }
    if (userCanSetFullScreen()) {
        allowed_actions |= NET::ActionFullScreen;
    }

    // Always (Pagers shouldn't show Docks etc.)
    allowed_actions |= NET::ActionChangeDesktop;

    if (isCloseable()) {
        allowed_actions |= NET::ActionClose;
    }
    if (old_allowed_actions == allowed_actions) {
        return;
    }

    // TODO: This could be delayed and compressed - It's only for pagers etc. anyway
    info->setAllowedActions(allowed_actions);

    // ONLY if relevant features have changed (and the window didn't just get/loose moveresize for maximization state changes)
    auto const relevant = ~(NET::ActionMove|NET::ActionResize);

    if ((allowed_actions & relevant) != (old_allowed_actions & relevant)) {
        if ((allowed_actions & NET::ActionMinimize) != (old_allowed_actions & NET::ActionMinimize)) {
            Q_EMIT minimizeableChanged(allowed_actions & NET::ActionMinimize);
        }
        if ((allowed_actions & NET::ActionShade) != (old_allowed_actions & NET::ActionShade)) {
            Q_EMIT shadeableChanged(allowed_actions & NET::ActionShade);
        }
        if ((allowed_actions & NET::ActionMax) != (old_allowed_actions & NET::ActionMax)) {
            Q_EMIT maximizeableChanged(allowed_actions & NET::ActionMax);
        }
    }
}

void X11Client::debug(QDebug& stream) const
{
    stream.nospace();
    print<QDebug>(stream);
}

Xcb::StringProperty X11Client::fetchActivities() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    return Xcb::StringProperty(window(), atoms->activities);
#else
    return Xcb::StringProperty();
#endif
}

void X11Client::readActivities(Xcb::StringProperty &property)
{
#ifdef KWIN_BUILD_ACTIVITIES
    QStringList newActivitiesList;
    QString prop = QString::fromUtf8(property);
    activitiesDefined = !prop.isEmpty();

    if (prop == Activities::nullUuid()) {
        //copied from setOnAllActivities to avoid a redundant XChangeProperty.
        if (!activityList.isEmpty()) {
            activityList.clear();
            updateActivities(true);
        }
        return;
    }
    if (prop.isEmpty()) {
        //note: this makes it *act* like it's on all activities but doesn't set the property to 'ALL'
        if (!activityList.isEmpty()) {
            activityList.clear();
            updateActivities(true);
        }
        return;
    }

    newActivitiesList = prop.split(u',');

    if (newActivitiesList == activityList)
        return; //expected change, it's ok.

    //otherwise, somebody else changed it. we need to validate before reacting.
    //if the activities are not synced, and there are existing clients with
    //activities specified, somebody has restarted kwin. we can not validate
    //activities in this case. we need to trust the old values.
    if (Activities::self() && Activities::self()->serviceStatus() != KActivities::Consumer::Unknown) {
        QStringList allActivities = Activities::self()->all();
        if (allActivities.isEmpty()) {
            qCDebug(KWIN_CORE) << "no activities!?!?";
            //don't touch anything, there's probably something bad going on and we don't wanna make it worse
            return;
        }


        for (int i = 0; i < newActivitiesList.size(); ++i) {
            if (! allActivities.contains(newActivitiesList.at(i))) {
                qCDebug(KWIN_CORE) << "invalid:" << newActivitiesList.at(i);
                newActivitiesList.removeAt(i--);
            }
        }
    }
    setOnActivities(newActivitiesList);
#else
    Q_UNUSED(property)
#endif
}

void X11Client::checkActivities()
{
#ifdef KWIN_BUILD_ACTIVITIES
    Xcb::StringProperty property = fetchActivities();
    readActivities(property);
#endif
}

void X11Client::setSessionActivityOverride(bool needed)
{
    sessionActivityOverride = needed;
    updateActivities(false);
}

Xcb::Property X11Client::fetchFirstInTabBox() const
{
    return Xcb::Property(false, m_client, atoms->kde_first_in_window_list,
                         atoms->kde_first_in_window_list, 0, 1);
}

void X11Client::readFirstInTabBox(Xcb::Property &property)
{
    control()->set_first_in_tabbox(property.toBool(32, atoms->kde_first_in_window_list));
}

void X11Client::updateFirstInTabBox()
{
    // TODO: move into KWindowInfo
    Xcb::Property property = fetchFirstInTabBox();
    readFirstInTabBox(property);
}

Xcb::StringProperty X11Client::fetchColorScheme() const
{
    return Xcb::StringProperty(m_client, atoms->kde_color_sheme);
}

void X11Client::readColorScheme(Xcb::StringProperty &property)
{
    win::set_color_scheme(this, control()->rules().checkDecoColor(QString::fromUtf8(property)));
}

void X11Client::updateColorScheme()
{
    Xcb::StringProperty property = fetchColorScheme();
    readColorScheme(property);
}

bool X11Client::isClient() const
{
    return true;
}

void X11Client::cancelFocusOutTimer()
{
    if (m_focusOutTimer) {
        m_focusOutTimer->stop();
    }
}

xcb_window_t X11Client::frameId() const
{
    return m_frame;
}

QRect X11Client::bufferGeometry() const
{
    return m_bufferGeometry;
}

QRect X11Client::frameRectToBufferRect(const QRect &rect) const
{
    if (win::decoration(this)) {
        return rect;
    }
    return win::frame_rect_to_client_rect(this, rect);
}

Xcb::Property X11Client::fetchShowOnScreenEdge() const
{
    return Xcb::Property(false, window(), atoms->kde_screen_edge_show, XCB_ATOM_CARDINAL, 0, 1);
}

void X11Client::readShowOnScreenEdge(Xcb::Property &property)
{
    //value comes in two parts, edge in the lower byte
    //then the type in the upper byte
    // 0 = autohide
    // 1 = raise in front on activate

    auto const value = property.value<uint32_t>(ElectricNone);
    ElectricBorder border = ElectricNone;

    switch (value & 0xFF) {
    case 0:
        border = ElectricTop;
        break;
    case 1:
        border = ElectricRight;
        break;
    case 2:
        border = ElectricBottom;
        break;
    case 3:
        border = ElectricLeft;
        break;
    }

    if (border != ElectricNone) {
        disconnect(m_edgeRemoveConnection);
        disconnect(m_edgeGeometryTrackingConnection);
        bool successfullyHidden = false;

        if (((value >> 8) & 0xFF) == 1) {
            win::set_keep_below(this, true);

            //request could have failed due to user kwin rules
            successfullyHidden = control()->keep_below();

            m_edgeRemoveConnection = connect(this, &Toplevel::keepBelowChanged, this, [this](){
                if (!control()->keep_below()) {
                    ScreenEdges::self()->reserve(this, ElectricNone);
                }
            });
        } else {
            hideClient(true);
            successfullyHidden = isHiddenInternal();

            m_edgeGeometryTrackingConnection = connect(this, &X11Client::geometryChanged, this, [this, border](){
                hideClient(true);
                ScreenEdges::self()->reserve(this, border);
            });
        }

        if (successfullyHidden) {
            ScreenEdges::self()->reserve(this, border);
        } else {
            ScreenEdges::self()->reserve(this, ElectricNone);
        }
    } else if (!property.isNull() && property->type != XCB_ATOM_NONE) {
        // property value is incorrect, delete the property
        // so that the client knows that it is not hidden
        xcb_delete_property(connection(), window(), atoms->kde_screen_edge_show);
    } else {
        // restore
        // TODO: add proper unreserve

        //this will call showOnScreenEdge to reset the state
        disconnect(m_edgeGeometryTrackingConnection);
        ScreenEdges::self()->reserve(this, ElectricNone);
    }
}

void X11Client::updateShowOnScreenEdge()
{
    Xcb::Property property = fetchShowOnScreenEdge();
    readShowOnScreenEdge(property);
}

void X11Client::showOnScreenEdge()
{
    disconnect(m_edgeRemoveConnection);

    hideClient(false);
    win::set_keep_below(this, false);
    xcb_delete_property(connection(), window(), atoms->kde_screen_edge_show);
}

void X11Client::addDamage(const QRegion &damage)
{
    if (!ready_for_painting) {
        // avoid "setReadyForPainting()" function calling overhead
        if (m_syncRequest.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            setReadyForPainting();
            win::setup_wayland_plasma_management(this);
        }
    }
    Toplevel::addDamage(damage);
}

bool X11Client::belongsToSameApplication(Toplevel const* other, win::same_client_check checks) const
{
    auto c2 = dynamic_cast<const X11Client*>(other);
    if (!c2) {
        return false;
    }
    return X11Client::belongToSameApplication(this, c2, checks);
}

QSize X11Client::resizeIncrements() const
{
    return m_geometryHints.resizeIncrements();
}

Xcb::StringProperty X11Client::fetchApplicationMenuServiceName() const
{
    return Xcb::StringProperty(m_client, atoms->kde_net_wm_appmenu_service_name);
}

void X11Client::readApplicationMenuServiceName(Xcb::StringProperty &property)
{
    control()->update_application_menu_service_name(QString::fromUtf8(property));
}

void X11Client::checkApplicationMenuServiceName()
{
    Xcb::StringProperty property = fetchApplicationMenuServiceName();
    readApplicationMenuServiceName(property);
}

Xcb::StringProperty X11Client::fetchApplicationMenuObjectPath() const
{
    return Xcb::StringProperty(m_client, atoms->kde_net_wm_appmenu_object_path);
}

void X11Client::readApplicationMenuObjectPath(Xcb::StringProperty &property)
{
    control()->update_application_menu_object_path(QString::fromUtf8(property));
}

void X11Client::checkApplicationMenuObjectPath()
{
    Xcb::StringProperty property = fetchApplicationMenuObjectPath();
    readApplicationMenuObjectPath(property);
}

void X11Client::handleSync()
{
    setReadyForPainting();
    win::setup_wayland_plasma_management(this);
    m_syncRequest.isPending = false;
    if (m_syncRequest.failsafeTimeout) {
        m_syncRequest.failsafeTimeout->stop();
    }
    if (win::is_resize(this)) {
        if (m_syncRequest.timeout) {
            m_syncRequest.timeout->stop();
        }
        win::perform_move_resize(this);
        updateWindowPixmap();
    } else {
        // setReadyForPainting does as well, but there's a small chance for resize syncs after the resize ended
        addRepaintFull();
    }
}

bool X11Client::belongToSameApplication(const X11Client *c1, const X11Client *c2, win::same_client_check checks)
{
    bool same_app = false;

    // tests that definitely mean they belong together
    if (c1 == c2) {
        same_app = true;
    } else if (c1->isTransient() && c2->transient()->has_child(c1, true)) {
        // c1 has c2 as mainwindow
        same_app = true;
    } else if (c2->isTransient() && c1->transient()->has_child(c2, true)) {
        // c2 has c1 as mainwindow
        same_app = true;
    } else if (c1->group() == c2->group()) {
         // same group
        same_app = true;
    } else if (c1->wmClientLeader() == c2->wmClientLeader()
            && c1->wmClientLeader() != c1->window()
            && c2->wmClientLeader() != c2->window()) {
        // if WM_CLIENT_LEADER is not set, it returns window(),
        // don't use in this test then same client leader
        same_app = true;

    // tests that mean they most probably don't belong together
    } else if ((c1->pid() != c2->pid() && !win::flags(checks & win::same_client_check::allow_cross_process))
            || c1->wmClientMachine(false) != c2->wmClientMachine(false)) {
        // different processes
    } else if (c1->wmClientLeader() != c2->wmClientLeader()
            && c1->wmClientLeader() != c1->window() // if WM_CLIENT_LEADER is not set, it returns window(),
            && c2->wmClientLeader() != c2->window() // don't use in this test then
            && !win::flags(checks & win::same_client_check::allow_cross_process)) {
        // different client leader
    } else if (!resourceMatch(c1, c2)) {
        // different apps
    } else if (!sameAppWindowRoleMatch(c1, c2, win::flags(checks & win::same_client_check::relaxed_for_active))
            && !win::flags(checks & win::same_client_check::allow_cross_process)) {
        // "different" apps
    } else if (c1->pid() == 0 || c2->pid() == 0) {
        // old apps that don't have _NET_WM_PID, consider them different
        // if they weren't found to match above
    } else {
        // looks like it's the same app
        same_app = true;
    }

    return same_app;
}

// TODO(romangg): is this still relevant today, i.e. 2020?
//
// Non-transient windows with window role containing '#' are always
// considered belonging to different applications (unless
// the window role is exactly the same). KMainWindow sets
// window role this way by default, and different KMainWindow
// usually "are" different application from user's point of view.
// This help with no-focus-stealing for e.g. konqy reusing.
// On the other hand, if one of the windows is active, they are
// considered belonging to the same application. This is for
// the cases when opening new mainwindow directly from the application,
// e.g. 'Open New Window' in konqy ( active_hack == true ).
bool X11Client::sameAppWindowRoleMatch(const X11Client *c1, const X11Client *c2, bool active_hack)
{
    if (c1->isTransient()) {
        while (auto const t = dynamic_cast<const X11Client*>(c1->transient()->lead())) {
            c1 = t;
        }
        if (c1->groupTransient()) {
            return c1->group() == c2->group();
        }
#if 0
        // if a group transient is in its own group, it didn't possibly have a group,
        // and therefore should be considered belonging to the same app like
        // all other windows from the same app
        || c1->group()->leaderClient() == c1 || c2->group()->leaderClient() == c2;
#endif
    }

    if (c2->isTransient()) {
        while (const X11Client *t = dynamic_cast<const X11Client *>(c2->transient()->lead()))
            c2 = t;
        if (c2->groupTransient())
            return c1->group() == c2->group();
#if 0
        || c1->group()->leaderClient() == c1 || c2->group()->leaderClient() == c2;
#endif
    }

    int pos1 = c1->windowRole().indexOf('#');
    int pos2 = c2->windowRole().indexOf('#');

    if ((pos1 >= 0 && pos2 >= 0)) {
        if (!active_hack) {
            // without the active hack for focus stealing prevention,
            // different mainwindows are always different apps
            return c1 == c2;
        }
        if (!c1->control()->active() && !c2->control()->active()) {
            return c1 == c2;
        }
    }
    return true;
}

/*

 Transiency stuff: ICCCM 4.1.2.6, NETWM 7.3

 WM_TRANSIENT_FOR is basically means "this is my mainwindow".
 For NET::Unknown windows, transient windows are considered to be NET::Dialog
 windows, for compatibility with non-NETWM clients. KWin may adjust the value
 of this property in some cases (window pointing to itself or creating a loop,
 keeping NET::Splash windows above other windows from the same app, etc.).

 X11Client::transient_for_id is the value of the WM_TRANSIENT_FOR property, after
 possibly being adjusted by KWin. X11Client::transient_for points to the Client
 this Client is transient for, or is NULL. If X11Client::transient_for_id is
 poiting to the root window, the window is considered to be transient
 for the whole window group, as suggested in NETWM 7.3.

 In the case of group transient window, X11Client::transient_for is NULL,
 and X11Client::groupTransient() returns true. Such window is treated as
 if it were transient for every window in its window group that has been
 mapped _before_ it (or, to be exact, was added to the same group before it).
 Otherwise two group transients can create loops, which can lead very very
 nasty things (bug #67914 and all its dupes).

 X11Client::original_transient_for_id is the value of the property, which
 may be different if X11Client::transient_for_id if e.g. forcing NET::Splash
 to be kept on top of its window group, or when the mainwindow is not mapped
 yet, in which case the window is temporarily made group transient,
 and when the mainwindow is mapped, transiency is re-evaluated.

 This can get a bit complicated with with e.g. two Konqueror windows created
 by the same process. They should ideally appear like two independent applications
 to the user. This should be accomplished by all windows in the same process
 having the same window group (needs to be changed in Qt at the moment), and
 using non-group transients poiting to their relevant mainwindow for toolwindows
 etc. KWin should handle both group and non-group transient dialogs well.

 In other words:
 - non-transient windows     : isTransient() == false
 - normal transients         : transientFor() != NULL
 - group transients          : groupTransient() == true

 - list of mainwindows       : mainClients()  (call once and loop over the result)
 - list of transients        : transients()
 - every window in the group : group()->members()
*/

Xcb::TransientFor X11Client::fetchTransient() const
{
    return Xcb::TransientFor(window());
}

void X11Client::readTransientProperty(Xcb::TransientFor &transientFor)
{
    xcb_window_t lead_id = XCB_WINDOW_NONE;

    bool failed = false;
    if (!transientFor.getTransientFor(&lead_id)) {
        lead_id = XCB_WINDOW_NONE;
        failed = true;
    }

    m_originalTransientForId = lead_id;
    lead_id = verifyTransientFor(lead_id, !failed);

    set_transient_lead(lead_id);
}

void X11Client::set_transient_lead(xcb_window_t lead_id)
{
    if (lead_id == m_transientForId) {
        return;
    }

    for (auto client : transient()->leads()) {
        client->transient()->remove_child(this);
    }

    X11Client* lead = nullptr;
    m_transientForId = lead_id;

    if (m_transientForId != XCB_WINDOW_NONE && !groupTransient()) {
        lead = workspace()->findClient(Predicate::WindowMatch, m_transientForId);
        assert(lead != nullptr);

        transient()->remove_child(lead);
        assert(!transient()->lead());

        lead->transient()->add_child(this);
    }

    checkGroup(nullptr);
    workspace()->updateClientLayer(this);
    workspace()->resetUpdateToolWindowsTimer();

}

void X11Client::cleanGrouping()
{
    m_transientForId = XCB_WINDOW_NONE;
    m_originalTransientForId = XCB_WINDOW_NONE;

    update_group(false);
}

/**
 * Updates the group transient relations between group members when this gets added or removed.
 */
void X11Client::update_group(bool add)
{
    assert(in_group);

    if (add) {
        if (!contains(in_group->members(), this)) {
            in_group->addMember(this);
        }\
        auto const is_gt = groupTransient();

        // This added window must be set as transient child for all windows that have no direct
        // or indirect transient relation with it (that way we ensure there are no cycles).
        for (auto member : in_group->members()) {
            if (member == this) {
                continue;
            }
            auto const member_is_gt = member->groupTransient();
            if (!is_gt && !member_is_gt) {
                continue;
            }

            if ((transient()->children.size() > 0 && member->transient()->is_follower_of(this))
                || (member->transient()->children.size() > 0
                    && transient()->is_follower_of(member))) {
                // A transitive relation already exists between member and this. Do not add
                // a group transient relation on top.
                continue;
            }

            if (is_gt) {
                // Prefer to add this (the new window to the group) as a child.
                member->transient()->add_child(this);
            } else {
                assert(member_is_gt);
                transient()->add_child(member);
            }
        }
    } else {
        in_group->ref();
        in_group->removeMember(this);

        for (auto win : in_group->members()) {
            if (m_transientForId == win->window()) {
                if (!contains(win->transient()->children, this)) {
                    win->transient()->add_child(this);
                }
            } else {
                win->transient()->remove_child(this);
            }
        }

        // Restore indirect group transient relations between members that have been cut off because
        // off the removal of this.
        for (auto& member : in_group->members()) {
            if (!member->groupTransient()) {
                continue;
            }

            for (auto lead : in_group->members()) {
                if (lead == member) {
                    continue;
                }
                if (!member->transient()->is_follower_of(lead) &&
                        !lead->transient()->is_follower_of(member)) {
                    // This is not fully correct since relative distances between indirect
                    // transients might be shuffeled but since X11 group transients are rarely used
                    // today let's ignore it for now.
                    lead->transient()->add_child(member);
                }
            }
        }

        in_group->deref();
        in_group = nullptr;
    }
}

/**
 * Check that the window is not transient for itself, and similar nonsense.
 */
xcb_window_t X11Client::verifyTransientFor(xcb_window_t new_transient_for, bool set)
{
    xcb_window_t new_property_value = new_transient_for;

    // make sure splashscreens are shown above all their app's windows, even though
    // they're in Normal layer
    if (win::is_splash(this) && new_transient_for == XCB_WINDOW_NONE) {
        new_transient_for = rootWindow();
    }

    if (new_transient_for == XCB_WINDOW_NONE) {
        if (set) {
            // sometimes WM_TRANSIENT_FOR is set to None, instead of root window
            new_property_value = new_transient_for = rootWindow();
        } else {
            return XCB_WINDOW_NONE;
        }
    }
    if (new_transient_for == window()) {
        // pointing to self
        // also fix the property itself
        qCWarning(KWIN_CORE) << "Client " << this << " has WM_TRANSIENT_FOR poiting to itself." ;
        new_property_value = new_transient_for = rootWindow();
    }

//  The transient_for window may be embedded in another application,
//  so kwin cannot see it. Try to find the managed client for the
//  window and fix the transient_for property if possible.
    auto before_search = new_transient_for;

    while (new_transient_for != XCB_WINDOW_NONE
            && new_transient_for != rootWindow()
            && !workspace()->findClient(Predicate::WindowMatch, new_transient_for)) {
        Xcb::Tree tree(new_transient_for);
        if (tree.isNull()) {
            break;
        }
        new_transient_for = tree->parent;
    }

    if (auto new_transient_for_client = workspace()->findClient(Predicate::WindowMatch, new_transient_for)) {
        if (new_transient_for != before_search) {
            qCDebug(KWIN_CORE) << "Client " << this << " has WM_TRANSIENT_FOR poiting to non-toplevel window "
                         << before_search << ", child of " << new_transient_for_client << ", adjusting.";

            // also fix the property
            new_property_value = new_transient_for;
        }
    } else {
        // nice try
        new_transient_for = before_search;
    }

// loop detection
// group transients cannot cause loops, because they're considered transient only for non-transient
// windows in the group
    int count = 20;
    auto loop_pos = new_transient_for;

    while (loop_pos != XCB_WINDOW_NONE && loop_pos != rootWindow()) {
        auto pos = workspace()->findClient(Predicate::WindowMatch, loop_pos);
        if (pos == nullptr) {
            break;
        }

        loop_pos = pos->m_transientForId;

        if (--count == 0 || pos == this) {
            qCWarning(KWIN_CORE) << "Client " << this << " caused WM_TRANSIENT_FOR loop." ;
            new_transient_for = rootWindow();
        }
    }

    if (new_transient_for != rootWindow()
            && workspace()->findClient(Predicate::WindowMatch, new_transient_for) == nullptr) {
        // it's transient for a specific window, but that window is not mapped
        new_transient_for = rootWindow();
    }

    if (new_property_value != m_originalTransientForId) {
        Xcb::setTransientFor(window(), new_property_value);
    }

    return new_transient_for;
}

// A new window has been mapped. Check if it's not a mainwindow for this already existing window.
void X11Client::checkTransient(Toplevel* window)
{
    auto id = window->window();
    if (m_originalTransientForId != id) {
        return;
    }
    id = verifyTransientFor(id, true);
    set_transient_lead(id);
}

Toplevel* X11Client::findModal()
{
    auto first_level_find = [](Toplevel* win) -> Toplevel* {
        auto find = [](Toplevel* win, auto& find_ref) -> Toplevel* {
            for (auto child : win->transient()->children) {
                if (auto ret = find_ref(child, find_ref)) {
                    return ret;
                }
            }
            return win->transient()->modal() ? win : nullptr;
        };

        return find(win, find);
    };

    for (auto child : transient()->children) {
        if (auto modal = first_level_find(child)) {
            return modal;
        }
    }

    return nullptr;
}

void X11Client::checkGroup(Group* group)
{
    // First get all information about the current group.
    if (!group) {
        auto lead = transient()->lead();

        if (lead) {
            // Move the window to the right group (e.g. a dialog provided
            // by this app, but transient for another, so make it part of that group).
            group = lead->group();
        } else if (info->groupLeader() != XCB_WINDOW_NONE) {
            group = workspace()->findGroup(info->groupLeader());
            if (!group) {
                // doesn't exist yet
                group = new Group(info->groupLeader());
            }
        } else {
            group = workspace()->findClientLeaderGroup(this);
            if (!group) {
                group = new Group(XCB_WINDOW_NONE);
            }
        }
    }

    if (in_group && in_group != group) {
        update_group(false);
    }

    in_group = group;

    if (in_group) {
        update_group(true);
    }

    checkActiveModal();
    workspace()->updateClientLayer(this);
}

// used by Workspace::findClientLeaderGroup()
void X11Client::changeClientLeaderGroup(Group* gr)
{
    // transient()->lead() != NULL are in the group of their mainwindow, so keep them there

    if (transient()->lead() != nullptr) {
        return;
    }

    // also don't change the group for window which have group set
    if (info->groupLeader()) {
        return;
    }

    // change group
    checkGroup(gr);
}

void X11Client::checkActiveModal()
{
    // If the active window got new modal transient, activate it.
    auto win = qobject_cast<X11Client*>(workspace()->mostRecentlyActivatedClient());
    if (!win) {
        return;
    }

    auto new_modal = qobject_cast<X11Client*>(win->findModal());

    if (new_modal && new_modal != win) {
        if (!new_modal->isManaged()) {
            // postpone check until end of manage()
            return;
        }
        workspace()->activateClient(new_modal);
    }
}

/**
 * Calculate the appropriate frame size for the given client size \a
 * wsize.
 *
 * \a wsize is adapted according to the window's size hints (minimum,
 * maximum and incremental size changes).
 */
QSize X11Client::sizeForClientSize(const QSize& wsize, win::size_mode mode, bool noframe) const
{
    int w = wsize.width();
    int h = wsize.height();

    if (w < 1 || h < 1) {
        qCWarning(KWIN_CORE) << "sizeForClientSize() with empty size!" ;
    }

    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }

    // basesize, minsize, maxsize, paspect and resizeinc have all values defined,
    // even if they're not set in flags - see getWmNormalHints()
    QSize min_size = minSize();
    QSize max_size = maxSize();

    if (win::decoration(this)) {
        QSize decominsize(0, 0);
        QSize border_size(win::left_border(this) + win::right_border(this),
                          win::top_border(this) + win::bottom_border(this));
        if (border_size.width() > decominsize.width()) {
            // just in case check
            decominsize.setWidth(border_size.width());
        }
        if (border_size.height() > decominsize.height()) {
            decominsize.setHeight(border_size.height());
        }
        if (decominsize.width() > min_size.width()) {
            min_size.setWidth(decominsize.width());
        }
        if (decominsize.height() > min_size.height()) {
            min_size.setHeight(decominsize.height());
        }
    }
    w = qMin(max_size.width(), w);
    h = qMin(max_size.height(), h);
    w = qMax(min_size.width(), w);
    h = qMax(min_size.height(), h);

    int w1 = w;
    int h1 = h;

    int width_inc = m_geometryHints.resizeIncrements().width();
    int height_inc = m_geometryHints.resizeIncrements().height();
    int basew_inc = m_geometryHints.baseSize().width();
    int baseh_inc = m_geometryHints.baseSize().height();

    if (!m_geometryHints.hasBaseSize()) {
        basew_inc = m_geometryHints.minSize().width();
        baseh_inc = m_geometryHints.minSize().height();
    }

    w = int((w - basew_inc) / width_inc) * width_inc + basew_inc;
    h = int((h - baseh_inc) / height_inc) * height_inc + baseh_inc;

// code for aspect ratios based on code from FVWM
    /*
     * The math looks like this:
     *
     * minAspectX    dwidth     maxAspectX
     * ---------- <= ------- <= ----------
     * minAspectY    dheight    maxAspectY
     *
     * If that is multiplied out, then the width and height are
     * invalid in the following situations:
     *
     * minAspectX * dheight > minAspectY * dwidth
     * maxAspectX * dheight < maxAspectY * dwidth
     *
     */
    if (m_geometryHints.hasAspect()) {
        // use doubles, because the values can be MAX_INT and multiplying would go wrong otherwise
        double min_aspect_w = m_geometryHints.minAspect().width();
        double min_aspect_h = m_geometryHints.minAspect().height();
        double max_aspect_w = m_geometryHints.maxAspect().width();
        double max_aspect_h = m_geometryHints.maxAspect().height();

        // According to ICCCM 4.1.2.3 PMinSize should be a fallback for PBaseSize for size increments,
        // but not for aspect ratio. Since this code comes from FVWM, handles both at the same time,
        // and I have no idea how it works, let's hope nobody relies on that.
        auto const baseSize = m_geometryHints.baseSize();

        w -= baseSize.width();
        h -= baseSize.height();

        int max_width = max_size.width() - baseSize.width();
        int min_width = min_size.width() - baseSize.width();
        int max_height = max_size.height() - baseSize.height();
        int min_height = min_size.height() - baseSize.height();

#define ASPECT_CHECK_GROW_W \
    if ( min_aspect_w * h > min_aspect_h * w ) \
    { \
        int delta = int( min_aspect_w * h / min_aspect_h - w ) / width_inc * width_inc; \
        if ( w + delta <= max_width ) \
            w += delta; \
    }

#define ASPECT_CHECK_SHRINK_H_GROW_W \
    if ( min_aspect_w * h > min_aspect_h * w ) \
    { \
        int delta = int( h - w * min_aspect_h / min_aspect_w ) / height_inc * height_inc; \
        if ( h - delta >= min_height ) \
            h -= delta; \
        else \
        { \
            int delta = int( min_aspect_w * h / min_aspect_h - w ) / width_inc * width_inc; \
            if ( w + delta <= max_width ) \
                w += delta; \
        } \
    }

#define ASPECT_CHECK_GROW_H \
    if ( max_aspect_w * h < max_aspect_h * w ) \
    { \
        int delta = int( w * max_aspect_h / max_aspect_w - h ) / height_inc * height_inc; \
        if ( h + delta <= max_height ) \
            h += delta; \
    }

#define ASPECT_CHECK_SHRINK_W_GROW_H \
    if ( max_aspect_w * h < max_aspect_h * w ) \
    { \
        int delta = int( w - max_aspect_w * h / max_aspect_h ) / width_inc * width_inc; \
        if ( w - delta >= min_width ) \
            w -= delta; \
        else \
        { \
            int delta = int( w * max_aspect_h / max_aspect_w - h ) / height_inc * height_inc; \
            if ( h + delta <= max_height ) \
                h += delta; \
        } \
    }

        switch(mode) {
        case win::size_mode::any:
#if 0
            // make SizeModeAny equal to SizeModeFixedW - prefer keeping fixed width,
            // so that changing aspect ratio to a different value and back keeps the same size (#87298)
            {
                ASPECT_CHECK_SHRINK_H_GROW_W
                ASPECT_CHECK_SHRINK_W_GROW_H
                ASPECT_CHECK_GROW_H
                ASPECT_CHECK_GROW_W
                break;
            }
#endif
        case win::size_mode::fixed_width: {
            // the checks are order so that attempts to modify height are first
            ASPECT_CHECK_GROW_H
            ASPECT_CHECK_SHRINK_H_GROW_W
            ASPECT_CHECK_SHRINK_W_GROW_H
            ASPECT_CHECK_GROW_W
            break;
        }
        case win::size_mode::fixed_height: {
            ASPECT_CHECK_GROW_W
            ASPECT_CHECK_SHRINK_W_GROW_H
            ASPECT_CHECK_SHRINK_H_GROW_W
            ASPECT_CHECK_GROW_H
            break;
        }
        case win::size_mode::max: {
            // first checks that try to shrink
            ASPECT_CHECK_SHRINK_H_GROW_W
            ASPECT_CHECK_SHRINK_W_GROW_H
            ASPECT_CHECK_GROW_W
            ASPECT_CHECK_GROW_H
            break;
        }
        }

#undef ASPECT_CHECK_SHRINK_H_GROW_W
#undef ASPECT_CHECK_SHRINK_W_GROW_H
#undef ASPECT_CHECK_GROW_W
#undef ASPECT_CHECK_GROW_H

        w += baseSize.width();
        h += baseSize.height();
    }

    if (!control()->rules().checkStrictGeometry(!control()->fullscreen())) {
        // disobey increments and aspect by explicit rule
        w = w1;
        h = h1;
    }

    QSize size(w, h);

    if (!noframe) {
        size = clientSizeToFrameSize(size);
    }

    return control()->rules().checkSize(size);
}

/**
 * Gets the client's normal WM hints and reconfigures itself respectively.
 */
void X11Client::getWmNormalHints()
{
    auto const hadFixedAspect = m_geometryHints.hasAspect();

    // roundtrip to X server
    m_geometryHints.fetch();
    m_geometryHints.read();

    if (!hadFixedAspect && m_geometryHints.hasAspect()) {
        // align to eventual new constraints
        win::maximize(this, max_mode);
    }

    if (isManaged()) {
        // update to match restrictions
        auto new_size = win::adjusted_size(this);

        if (new_size != size() && !control()->fullscreen()) {
            auto origClientGeometry = m_clientGeometry;

            resizeWithChecks(new_size);

            if ((!win::is_special_window(this) || win::is_toolbar(this)) && !control()->fullscreen()) {
                // try to keep the window in its xinerama screen if possible,
                // if that fails at least keep it visible somewhere
                QRect area = workspace()->clientArea(MovementArea, this);
                if (area.contains(origClientGeometry))
                    win::keep_in_area(this, area, false);
                area = workspace()->clientArea(WorkArea, this);
                if (area.contains(origClientGeometry))
                    win::keep_in_area(this, area, false);
            }
        }
    }

    // affects isResizeable()
    updateAllowedActions();
}

QSize X11Client::minSize() const
{
    return control()->rules().checkMinSize(m_geometryHints.minSize());
}

QSize X11Client::maxSize() const
{
    return control()->rules().checkMaxSize(m_geometryHints.maxSize());
}

QSize X11Client::basicUnit() const
{
    return m_geometryHints.resizeIncrements();
}

/**
 * Auxiliary function to inform the client about the current window
 * configuration.
 */
void X11Client::sendSyntheticConfigureNotify()
{
    xcb_configure_notify_event_t c;
    memset(&c, 0, sizeof(c));
    c.response_type = XCB_CONFIGURE_NOTIFY;
    c.event = window();
    c.window = window();
    c.x = m_clientGeometry.x();
    c.y = m_clientGeometry.y();

    c.width = m_clientGeometry.width();
    c.height = m_clientGeometry.height();
    auto getEmulatedXWaylandSize = [this]() {
        auto property = Xcb::Property(false, window(),
                                      atoms->xwayland_randr_emu_monitor_rects, XCB_ATOM_CARDINAL,
                                      0, 1000);
        if (!property) {
            return QSize();
        }
        auto rects = property.value<uint32_t*>();

        if (property->value_len % 4) {
            return QSize();
        }

        for (uint32_t i = 0; i < property->value_len / 4; i++) {
            auto r = &rects[i];

            if (r[0] - m_clientGeometry.x() == 0 && r[1] - m_clientGeometry.y() == 0) {
                return QSize(r[2], r[3]);
            }
        }
        return QSize();
    };

    if (control()->fullscreen()) {
        // Workaround for XWayland clients setting fullscreen
        auto const emulatedSize = getEmulatedXWaylandSize();

        if (emulatedSize.isValid()) {
            c.width = emulatedSize.width();
            c.height = emulatedSize.height();

            uint32_t const values[] = { c.width, c.height };
            ScopedCPointer<xcb_generic_error_t> error(xcb_request_check(connection(),
                xcb_configure_window_checked(connection(), c.window,
                                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                             values)));
            if (!error.isNull()) {
                qCDebug(KWIN_CORE) << "Error on emulating XWayland size: " << error->error_code;
            }
        }
    }

    c.border_width = 0;
    c.above_sibling = XCB_WINDOW_NONE;
    c.override_redirect = 0;

    xcb_send_event(connection(), true, c.event, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char*>(&c));
    xcb_flush(connection());
}

QPoint X11Client::gravityAdjustment(xcb_gravity_t gravity) const
{
    int dx = 0;
    int dy = 0;

    // dx, dy specify how the client window moves to make space for the frame.
    // In general we have to compute the reference point and from that figure
    // out how much we need to shift the client, however given that we ignore
    // the border width attribute and the extents of the server-side decoration
    // are known in advance, we can simplify the math quite a bit and express
    // the required window gravity adjustment in terms of border sizes.
    switch(gravity) {
    case XCB_GRAVITY_NORTH_WEST:
        // move down right
    default:
        dx = win::left_border(this);
        dy = win::top_border(this);
        break;
    case XCB_GRAVITY_NORTH:
        // move right
        dx = 0;
        dy = win::top_border(this);
        break;
    case XCB_GRAVITY_NORTH_EAST:
        // move down left
        dx = -win::right_border(this);
        dy = win::top_border(this);
        break;
    case XCB_GRAVITY_WEST:
        // move right
        dx = win::left_border(this);
        dy = 0;
        break;
    case XCB_GRAVITY_CENTER:
        dx = (win::left_border(this) - win::right_border(this)) / 2;
        dy = (win::top_border(this) - win::bottom_border(this)) / 2;
        break;
    case XCB_GRAVITY_STATIC:
        // don't move
        dx = 0;
        dy = 0;
        break;
    case XCB_GRAVITY_EAST:
        // move left
        dx = -win::right_border(this);
        dy = 0;
        break;
    case XCB_GRAVITY_SOUTH_WEST:
        // move up right
        dx = win::left_border(this) ;
        dy = -win::bottom_border(this);
        break;
    case XCB_GRAVITY_SOUTH:
        // move up
        dx = 0;
        dy = -win::bottom_border(this);
        break;
    case XCB_GRAVITY_SOUTH_EAST:
        // move up left
        dx = -win::right_border(this);
        dy = -win::bottom_border(this);
        break;
    }

    return QPoint(dx, dy);
}

const QPoint X11Client::calculateGravitation(bool invert) const
{
    const QPoint adjustment = gravityAdjustment(m_geometryHints.windowGravity());

    // translate from client movement to frame movement
    auto const dx = adjustment.x() - win::left_border(this);
    auto const dy = adjustment.y() - win::top_border(this);

    if (invert) {
        return QPoint(pos().x() - dx, pos().y() - dy);
    }
    return QPoint(pos().x() + dx, pos().y() + dy);
}

void X11Client::configureRequest(int value_mask, int rx, int ry, int rw, int rh, int gravity, bool from_tool)
{
    auto const configurePositionMask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    auto const configureSizeMask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    auto const configureGeometryMask = configurePositionMask | configureSizeMask;

    // "maximized" is a user setting -> we do not allow the client to resize itself
    // away from this & against the users explicit wish
    qCDebug(KWIN_CORE) << this << bool(value_mask & configureGeometryMask) <<
                            bool(maximizeMode() & win::maximize_mode::vertical) <<
                            bool(maximizeMode() & win::maximize_mode::horizontal);

    // we want to (partially) ignore the request when the window is somehow maximized or quicktiled
    auto ignore = !app_noborder
        && (control()->quicktiling() != win::quicktiles::none
            || maximizeMode() != win::maximize_mode::restore);

    // however, the user shall be able to force obedience despite and also disobedience in general
    ignore = control()->rules().checkIgnoreGeometry(ignore);

    if (!ignore) {
        // either we're not max'd / q'tiled or the user allowed the client to break that - so break it.
        control()->set_quicktiling(win::quicktiles::none);
        max_mode = win::maximize_mode::restore;
        Q_EMIT quicktiling_changed();
    } else if (!app_noborder && control()->quicktiling() == win::quicktiles::none &&
        (maximizeMode() == win::maximize_mode::vertical || maximizeMode() == win::maximize_mode::horizontal)) {
        // ignoring can be, because either we do, or the user does explicitly not want it.
        // for partially maximized windows we want to allow configures in the other dimension.
        // so we've to ask the user again - to know whether we just ignored for the partial maximization.
        // the problem here is, that the user can explicitly permit configure requests - even for maximized windows!
        // we cannot distinguish that from passing "false" for partially maximized windows.
        ignore = control()->rules().checkIgnoreGeometry(false);

        if (!ignore) {
            // the user is not interested, so we fix up dimensions
            if (maximizeMode() == win::maximize_mode::vertical) {
                value_mask &= ~(XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT);
            }
            if (maximizeMode() == win::maximize_mode::horizontal) {
                value_mask &= ~(XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_WIDTH);
            }
            if (!(value_mask & configureGeometryMask)) {
                // the modification turned the request void
                ignore = true;
            }
        }
    }

    if (ignore) {
        // nothing to (left) to do for use - bugs #158974, #252314, #321491
        qCDebug(KWIN_CORE) << "DENIED";
        return;
    }

    qCDebug(KWIN_CORE) << "PERMITTED" << this << bool(value_mask & configureGeometryMask);

    if (gravity == 0) {
        // default (nonsense) value for the argument
        gravity = m_geometryHints.windowGravity();
    }

    if (value_mask & configurePositionMask) {
        auto new_pos = framePosToClientPos(pos());
        new_pos -= gravityAdjustment(xcb_gravity_t(gravity));

        if (value_mask & XCB_CONFIG_WINDOW_X) {
            new_pos.setX(rx);
        }
        if (value_mask & XCB_CONFIG_WINDOW_Y) {
            new_pos.setY(ry);
        }

        // clever(?) workaround for applications like xv that want to set
        // the location to the current location but miscalculate the
        // frame size due to kwin being a double-reparenting window
        // manager
        if (new_pos.x() == m_clientGeometry.x() && new_pos.y() == m_clientGeometry.y()
                && gravity == XCB_GRAVITY_NORTH_WEST && !from_tool) {
            new_pos.setX(pos().x());
            new_pos.setY(pos().y());
        }

        new_pos += gravityAdjustment(xcb_gravity_t(gravity));
        new_pos = clientPosToFramePos(new_pos);

        int nw = clientSize().width();
        int nh = clientSize().height();

        if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
            nw = rw;
        }
        if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
            nh = rh;
        }

        // enforces size if needed
        auto ns = sizeForClientSize(QSize(nw, nh));
        new_pos = control()->rules().checkPosition(new_pos);
        int newScreen = screens()->number(QRect(new_pos, ns).center());

        if (newScreen != control()->rules().checkScreen(newScreen)) {
            // not allowed by rule
            return;
        }

        auto origClientGeometry = m_clientGeometry;
        win::geometry_updates_blocker blocker(this);
        win::move(this, new_pos);
        plainResize(ns);

        auto area = workspace()->clientArea(WorkArea, this);

        if (!from_tool && (!win::is_special_window(this) || win::is_toolbar(this))
                && !control()->fullscreen() && area.contains(origClientGeometry)) {
            win::keep_in_area(this, area, false);
        }

        // this is part of the kicker-xinerama-hack... it should be
        // safe to remove when kicker gets proper ExtendedStrut support;
        // see Workspace::updateClientArea() and
        // X11Client::adjustedClientArea()
        if (hasStrut()) {
            workspace() -> updateClientArea();
        }
    }

    if (value_mask & configureSizeMask && !(value_mask & configurePositionMask)) {
        // pure resize
        int nw = clientSize().width();
        int nh = clientSize().height();

        if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
            nw = rw;
        }
        if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
            nh = rh;
        }

        auto ns = sizeForClientSize(QSize(nw, nh));

        if (ns != size()) {
            // don't restore if some app sets its own size again
            auto origClientGeometry = m_clientGeometry;
            win::geometry_updates_blocker blocker(this);
            resizeWithChecks(ns, xcb_gravity_t(gravity));

            if (!from_tool && (!win::is_special_window(this) || win::is_toolbar(this))
                    && !control()->fullscreen()) {
                // try to keep the window in its xinerama screen if possible,
                // if that fails at least keep it visible somewhere

                auto area = workspace()->clientArea(MovementArea, this);
                if (area.contains(origClientGeometry)) {
                    win::keep_in_area(this, area, false);
                }

                area = workspace()->clientArea(WorkArea, this);
                if (area.contains(origClientGeometry)) {
                    win::keep_in_area(this, area, false);
                }
            }
        }
    }

    restore_geometries.maximize = frameGeometry();

    // No need to send synthetic configure notify event here, either it's sent together
    // with geometry change, or there's no need to send it.
    // Handling of the real ConfigureRequest event forces sending it, as there it's necessary.
}

void X11Client::resizeWithChecks(int w, int h, xcb_gravity_t gravity, win::force_geometry force)
{
    assert(!shade_geometry_change);

    if (win::shaded(this)) {
        if (h == win::top_border(this) + win::bottom_border(this)) {
            qCWarning(KWIN_CORE) << "Shaded geometry passed for size:" ;
        }
    }

    int newx = pos().x();
    int newy = pos().y();

    auto area = workspace()->clientArea(WorkArea, this);

    // don't allow growing larger than workarea
    if (w > area.width()) {
        w = area.width();
    }
    if (h > area.height()) {
        h = area.height();
    }

    // checks size constraints, including min/max size
    auto tmp = win::adjusted_size(this, QSize(w, h), win::size_mode::any);
    w = tmp.width();
    h = tmp.height();

    if (gravity == 0) {
        gravity = m_geometryHints.windowGravity();
    }

    switch(gravity) {
    case XCB_GRAVITY_NORTH_WEST:
        // top left corner doesn't move
    default:
        break;
    case XCB_GRAVITY_NORTH:
        // middle of top border doesn't move
        newx = (newx + size().width() / 2) - (w / 2);
        break;
    case XCB_GRAVITY_NORTH_EAST:
        // top right corner doesn't move
        newx = newx + size().width() - w;
        break;
    case XCB_GRAVITY_WEST:
        // middle of left border doesn't move
        newy = (newy + size().height() / 2) - (h / 2);
        break;
    case XCB_GRAVITY_CENTER:
        // middle point doesn't move
        newx = (newx + size().width() / 2) - (w / 2);
        newy = (newy + size().height() / 2) - (h / 2);
        break;
    case XCB_GRAVITY_STATIC:
        // top left corner of _client_ window doesn't move
        // since decoration doesn't change, equal to NorthWestGravity
        break;
    case XCB_GRAVITY_EAST:
        // middle of right border doesn't move
        newx = newx + size().width() - w;
        newy = (newy + size().height() / 2) - (h / 2);
        break;
    case XCB_GRAVITY_SOUTH_WEST:
        // bottom left corner doesn't move
        newy = newy + size().height() - h;
        break;
    case XCB_GRAVITY_SOUTH:
        // middle of bottom border doesn't move
        newx = (newx + size().width() / 2) - (w / 2);
        newy = newy + size().height() - h;
        break;
    case XCB_GRAVITY_SOUTH_EAST:
        // bottom right corner doesn't move
        newx = newx + size().width() - w;
        newy = newy + size().height() - h;
        break;
    }

    setFrameGeometry(QRect(newx, newy, w, h), force);
}

// _NET_MOVERESIZE_WINDOW
void X11Client::NETMoveResizeWindow(int flags, int x, int y, int width, int height)
{
    int gravity = flags & 0xff;
    int value_mask = 0;

    if (flags & (1 << 8)) {
        value_mask |= XCB_CONFIG_WINDOW_X;
    }
    if (flags & (1 << 9)) {
        value_mask |= XCB_CONFIG_WINDOW_Y;
    }
    if (flags & (1 << 10)) {
        value_mask |= XCB_CONFIG_WINDOW_WIDTH;
    }
    if (flags & (1 << 11)) {
        value_mask |= XCB_CONFIG_WINDOW_HEIGHT;
    }

    configureRequest(value_mask, x, y, width, height, gravity, true);
}

bool X11Client::isMovable() const
{
    if (!hasNETSupport() && !m_motif.move()) {
        return false;
    }
    if (control()->fullscreen()) {
        return false;
    }
    if (win::is_special_window(this) && !win::is_splash(this) && !win::is_toolbar(this))   {
        // allow moving of splashscreens :)
        return false;
    }
    if (control()->rules().checkPosition(invalidPoint) != invalidPoint) {
        // forced position
        return false;
    }
    return true;
}

bool X11Client::isMovableAcrossScreens() const
{
    if (!hasNETSupport() && !m_motif.move()) {
        return false;
    }
    if (win::is_special_window(this) && !win::is_splash(this) && !win::is_toolbar(this)) {
        // allow moving of splashscreens :)
        return false;
    }
    if (control()->rules().checkPosition(invalidPoint) != invalidPoint) {
        // forced position
        return false;
    }
    return true;
}

bool X11Client::isResizable() const
{
    if (!hasNETSupport() && !m_motif.resize()) {
        return false;
    }
    if (control()->fullscreen()) {
        return false;
    }
    if (win::is_special_window(this) || win::is_splash(this) || win::is_toolbar(this)) {
        return false;
    }
    if (control()->rules().checkSize(QSize()).isValid()) {
        // forced size
        return false;
    }

    auto const mode = control()->move_resize().contact;

    // TODO: we could just check with & on top and left.
    if ((mode == win::position::top || mode == win::position::top_left || mode == win::position::top_right ||
         mode == win::position::left || mode == win::position::bottom_left) && control()->rules().checkPosition(invalidPoint) != invalidPoint) {
        return false;
    }

    auto min = minSize();
    auto max = maxSize();

    return min.width() < max.width() || min.height() < max.height();
}

bool X11Client::isMaximizable() const
{
    if (!isResizable() || win::is_toolbar(this)) {
        // SELI isToolbar() ?
        return false;
    }
    if (control()->rules().checkMaximize(win::maximize_mode::restore) == win::maximize_mode::restore
            && control()->rules().checkMaximize(win::maximize_mode::full) != win::maximize_mode::restore) {
        return true;
    }
    return false;
}


/**
 * Reimplemented to inform the client about the new window position.
 */
void X11Client::setFrameGeometry(QRect const& rect, win::force_geometry force)
{
    // this code is also duplicated in X11Client::plainResize()
    // Ok, the shading geometry stuff. Generally, code doesn't care about shaded geometry,
    // simply because there are too many places dealing with geometry. Those places
    // ignore shaded state and use normal geometry, which they usually should get
    // from adjustedSize(). Such geometry comes here, and if the window is shaded,
    // the geometry is used only for client_size, since that one is not used when
    // shading. Then the frame geometry is adjusted for the shaded geometry.
    // This gets more complicated in the case the code does only something like
    // setGeometry( geometry()) - geometry() will return the shaded frame geometry.
    // Such code is wrong and should be changed to handle the case when the window is shaded,
    // for example using X11Client::clientSize()

    auto frameGeometry = rect;

    if (shade_geometry_change) {
        // nothing
    } else if (win::shaded(this)) {
        if (frameGeometry.height() == win::top_border(this) + win::bottom_border(this)) {
            qCDebug(KWIN_CORE) << "Shaded geometry passed for size:";
        } else {
            m_clientGeometry = win::frame_rect_to_client_rect(this, frameGeometry);
            frameGeometry.setHeight(win::top_border(this) + win::bottom_border(this));
        }
    } else {
        m_clientGeometry = win::frame_rect_to_client_rect(this, frameGeometry);
    }

    auto const bufferGeometry = frameRectToBufferRect(frameGeometry);
    if (!control()->geometry_updates_blocked() && frameGeometry != control()->rules().checkGeometry(frameGeometry)) {
        qCDebug(KWIN_CORE) << "forced geometry fail:" << frameGeometry << ":" << control()->rules().checkGeometry(frameGeometry);
    }

    set_frame_geometry(frameGeometry);
    if (force == win::force_geometry::no && m_bufferGeometry == bufferGeometry &&
            control()->pending_geometry_update() == win::pending_geometry::none) {
        return;
    }

    m_bufferGeometry = bufferGeometry;

    if (control()->geometry_updates_blocked()) {
        if (control()->pending_geometry_update() == win::pending_geometry::forced) {
            // maximum, nothing needed
        } else if (force == win::force_geometry::yes) {
            control()->set_pending_geometry_update(win::pending_geometry::forced);
        } else {
            control()->set_pending_geometry_update(win::pending_geometry::normal);
        }
        return;
    }

    updateServerGeometry();
    updateWindowRules(Rules::Position|Rules::Size);

    // keep track of old maximize mode
    // to detect changes
    screens()->setCurrent(this);
    workspace()->updateStackingOrder();

    // Need to regenerate decoration pixmaps when the buffer size is changed.
    if (control()->buffer_geometry_before_update_blocking().size() != m_bufferGeometry.size()) {
        discardWindowPixmap();
    }

    Q_EMIT geometryShapeChanged(this, control()->frame_geometry_before_update_blocking());
    win::add_repaint_during_geometry_updates(this);
    control()->update_geometry_before_update_blocking();

    // TODO: this signal is emitted too often
    Q_EMIT geometryChanged();
}

void X11Client::plainResize(int w, int h, win::force_geometry force)
{
    QSize frameSize(w, h);
    QSize bufferSize;

    // this code is also duplicated in X11Client::setGeometry(), and it's also commented there
    if (shade_geometry_change) {
        // nothing
    } else if (win::shaded(this)) {
        if (frameSize.height() == win::top_border(this) + win::bottom_border(this)) {
            qCDebug(KWIN_CORE) << "Shaded geometry passed for size:";
        } else {
            m_clientGeometry.setSize(frameSizeToClientSize(frameSize));
            frameSize.setHeight(win::top_border(this) + win::bottom_border(this));
        }
    } else {
        m_clientGeometry.setSize(frameSizeToClientSize(frameSize));
    }
    if (win::decoration(this)) {
        bufferSize = frameSize;
    } else {
        bufferSize = m_clientGeometry.size();
    }
    if (!control()->geometry_updates_blocked() && frameSize != control()->rules().checkSize(frameSize)) {
        qCDebug(KWIN_CORE) << "forced size fail:" << frameSize << ":" << control()->rules().checkSize(frameSize);
    }

    set_frame_geometry(QRect(frameGeometry().topLeft(), frameSize));

    // resuming geometry updates is handled only in setGeometry()
    assert(control()->pending_geometry_update() == win::pending_geometry::none ||
             control()->geometry_updates_blocked());

    if (force == win::force_geometry::no && m_bufferGeometry.size() == bufferSize) {
        return;
    }

    m_bufferGeometry.setSize(bufferSize);

    if (control()->geometry_updates_blocked()) {
        if (control()->pending_geometry_update() == win::pending_geometry::forced) {
            // maximum, nothing needed
        } else if (force == win::force_geometry::yes) {
            control()->set_pending_geometry_update(win::pending_geometry::forced);
        } else {
            control()->set_pending_geometry_update(win::pending_geometry::normal);
        }
        return;
    }

    updateServerGeometry();
    updateWindowRules(Rules::Position|Rules::Size);
    screens()->setCurrent(this);
    workspace()->updateStackingOrder();

    if (control()->buffer_geometry_before_update_blocking().size() != m_bufferGeometry.size()) {
        discardWindowPixmap();
    }

    Q_EMIT geometryShapeChanged(this, control()->frame_geometry_before_update_blocking());
    win::add_repaint_during_geometry_updates(this);
    control()->update_geometry_before_update_blocking();

    // TODO: this signal is emitted too often
    Q_EMIT geometryChanged();
}

void X11Client::updateServerGeometry()
{
    auto const oldBufferGeometry = control()->buffer_geometry_before_update_blocking();

    if (oldBufferGeometry.size() != m_bufferGeometry.size() || control()->pending_geometry_update() == win::pending_geometry::forced) {
        resizeDecoration();
        // If the client is being interactively resized, then the frame window, the wrapper window,
        // and the client window have correct geometry at this point, so we don't have to configure
        // them again. If the client doesn't support frame counters, always update geometry.
        auto const needsGeometryUpdate = !win::is_resize(this) || m_syncRequest.counter == XCB_NONE;

        if (needsGeometryUpdate) {
            m_frame.setGeometry(m_bufferGeometry);
        }

        if (!win::shaded(this)) {
            if (needsGeometryUpdate) {
                m_wrapper.setGeometry(QRect(win::to_client_pos(this, QPoint()), clientSize()));
                m_client.resize(clientSize());
            }
            // SELI - won't this be too expensive?
            // THOMAS - yes, but gtk+ clients will not resize without ...
            sendSyntheticConfigureNotify();
        }

        updateShape();
    } else {
        if (control()->move_resize().enabled) {
            if (win::compositing()) {
                // Defer the X update until we leave this mode
                needsXWindowMove = true;
            } else {
                // sendSyntheticConfigureNotify() on finish shall be sufficient
                m_frame.move(m_bufferGeometry.topLeft());
            }
        } else {
            m_frame.move(m_bufferGeometry.topLeft());
            sendSyntheticConfigureNotify();
        }

        // Unconditionally move the input window: it won't affect rendering
        m_decoInputExtent.move(pos() + inputPos());
    }
}

static bool changeMaximizeRecursion = false;

void X11Client::changeMaximize(bool horizontal, bool vertical, bool adjust)
{
    if (changeMaximizeRecursion) {
        return;
    }

    if (!isResizable() || win::is_toolbar(this)) {
        // SELI isToolbar() ?
        return;
    }

    QRect clientArea;
    if (control()->electric_maximizing()) {
        clientArea = workspace()->clientArea(MaximizeArea, Cursor::pos(), desktop());
    } else {
        clientArea = workspace()->clientArea(MaximizeArea, this);
    }

    auto old_mode = max_mode;

    // 'adjust == true' means to update the size only, e.g. after changing workspace size
    if (!adjust) {
        if (vertical)
            max_mode = max_mode ^ win::maximize_mode::vertical;
        if (horizontal)
            max_mode = max_mode ^ win::maximize_mode::horizontal;
    }

    // if the client insist on a fix aspect ratio, we check whether the maximizing will get us
    // out of screen bounds and take that as a "full maximization with aspect check" then
    if (m_geometryHints.hasAspect() &&
        (max_mode == win::maximize_mode::vertical || max_mode == win::maximize_mode::horizontal) &&
        control()->rules().checkStrictGeometry(true)) {
        // fixed aspect; on dimensional maximization obey aspect
        auto const minAspect = m_geometryHints.minAspect();
        auto const maxAspect = m_geometryHints.maxAspect();

        if (max_mode == win::maximize_mode::vertical || win::flags(old_mode & win::maximize_mode::vertical)) {
            // use doubles, because the values can be MAX_INT
            double const fx = minAspect.width();
            double const fy = maxAspect.height();

            if (fx*clientArea.height()/fy > clientArea.width()) {
                // too big
                max_mode = win::flags(old_mode & win::maximize_mode::horizontal) ?
                    win::maximize_mode::restore : win::maximize_mode::full;
            }
        } else {
            // max_mode == win::maximize_mode::horizontal
            double const fx = maxAspect.width();
            double const fy = minAspect.height();
            if (fy*clientArea.width()/fx > clientArea.height()) {
                // too big
                max_mode = win::flags(old_mode & win::maximize_mode::vertical) ?
                    win::maximize_mode::restore : win::maximize_mode::full;
            }
        }
    }

    max_mode = control()->rules().checkMaximize(max_mode);

    if (!adjust && max_mode == old_mode) {
        return;
    }

    win::geometry_updates_blocker blocker(this);

    // maximing one way and unmaximizing the other way shouldn't happen,
    // so restore first and then maximize the other way
    if ((old_mode == win::maximize_mode::vertical && max_mode == win::maximize_mode::horizontal)
            || (old_mode == win::maximize_mode::horizontal && max_mode == win::maximize_mode::vertical)) {
        // restore
        changeMaximize(false, false, false);
    }

    // save sizes for restoring, if maximalizing
    QSize sz;
    if (win::shaded(this)) {
        sz = sizeForClientSize(clientSize());
    } else {
        sz = size();
    }

    if (control()->quicktiling() == win::quicktiles::none) {
        if (!adjust && !win::flags(old_mode & win::maximize_mode::vertical)) {
            restore_geometries.maximize.setTop(pos().y());
            restore_geometries.maximize.setHeight(sz.height());
        }
        if (!adjust && !win::flags(old_mode & win::maximize_mode::horizontal)) {
            restore_geometries.maximize.setLeft(pos().x());
            restore_geometries.maximize.setWidth(sz.width());
        }
    }

    // call into decoration update borders
    if (win::decoration(this) && win::decoration(this)->client() && !(options->borderlessMaximizedWindows() && max_mode == win::maximize_mode::full)) {
        changeMaximizeRecursion = true;
        auto const c = win::decoration(this)->client().toStrongRef().data();

        if ((max_mode & win::maximize_mode::vertical) != (old_mode & win::maximize_mode::vertical)) {
            Q_EMIT c->maximizedVerticallyChanged(win::flags(max_mode & win::maximize_mode::vertical));
        }
        if ((max_mode & win::maximize_mode::horizontal) != (old_mode & win::maximize_mode::horizontal)) {
            Q_EMIT c->maximizedHorizontallyChanged(win::flags(max_mode & win::maximize_mode::horizontal));
        }
        if ((max_mode == win::maximize_mode::full) != (old_mode == win::maximize_mode::full)) {
            Q_EMIT c->maximizedChanged(win::flags(max_mode & win::maximize_mode::full));
        }

        changeMaximizeRecursion = false;
    }

    if (options->borderlessMaximizedWindows()) {
        // triggers a maximize change.
        // The next setNoBorder interation will exit since there's no change but the first recursion pullutes the restore geometry
        changeMaximizeRecursion = true;
        setNoBorder(control()->rules().checkNoBorder(app_noborder || (m_motif.hasDecoration() && m_motif.noBorder()) || max_mode == win::maximize_mode::full));
        changeMaximizeRecursion = false;
    }

    auto const geom_mode = win::decoration(this) ? win::force_geometry::yes : win::force_geometry::no;

    // Conditional quick tiling exit points
    if (control()->quicktiling() != win::quicktiles::none) {
        if (old_mode == win::maximize_mode::full &&
                !clientArea.contains(restore_geometries.maximize.center())) {
            // Not restoring on the same screen
            // TODO: The following doesn't work for some reason
            //quick_tile_mode = win::quicktiles::none; // And exit quick tile mode manually
        } else if ((old_mode == win::maximize_mode::vertical && max_mode == win::maximize_mode::restore) ||
                  (old_mode == win::maximize_mode::full && max_mode == win::maximize_mode::horizontal)) {
            // Modifying geometry of a tiled window
            // Exit quick tile mode without restoring geometry
            control()->set_quicktiling(win::quicktiles::none);
        }
    }

    auto& restore_geo = restore_geometries.maximize;

    switch(max_mode) {
    case win::maximize_mode::vertical: {
        if (win::flags(old_mode & win::maximize_mode::horizontal)) {
            // actually restoring from win::maximize_mode::full
            if (restore_geo.width() == 0 || !clientArea.contains(restore_geo.center())) {
                // needs placement
                plainResize(win::adjusted_size(this,
                                               QSize(size().width() * 2 / 3, clientArea.height()),
                                               win::size_mode::fixed_height),
                            geom_mode);
                Placement::self()->placeSmart(this, clientArea);
            } else {
                setFrameGeometry(QRect(QPoint(restore_geo.x(), clientArea.top()),
                                       win::adjusted_size(this,
                                                          QSize(restore_geo.width(),
                                                                clientArea.height()),
                                                          win::size_mode::fixed_height)),
                                 geom_mode);
            }
        } else {
            QRect r(pos().x(), clientArea.top(), size().width(), clientArea.height());
            r.setTopLeft(control()->rules().checkPosition(r.topLeft()));
            r.setSize(win::adjusted_size(this, r.size(), win::size_mode::fixed_height));
            setFrameGeometry(r, geom_mode);
        }
        info->setState(NET::MaxVert, NET::Max);
        break;
    }

    case win::maximize_mode::horizontal: {
        if (win::flags(old_mode & win::maximize_mode::vertical)) {
            // actually restoring from win::maximize_mode::full
            if (restore_geo.height() == 0 || !clientArea.contains(restore_geo.center())) {
                // needs placement
                plainResize(win::adjusted_size(this,
                                               QSize(clientArea.width(), size().height() * 2 / 3),
                                               win::size_mode::fixed_width),
                            geom_mode);
                Placement::self()->placeSmart(this, clientArea);
            } else {
                setFrameGeometry(QRect(QPoint(clientArea.left(), restore_geo.y()),
                                       win::adjusted_size(this, QSize(clientArea.width(),
                                                                      restore_geo.height()),
                                                          win::size_mode::fixed_width)),
                                 geom_mode);
            }
        } else {
            QRect r(clientArea.left(), pos().y(), clientArea.width(), size().height());
            r.setTopLeft(control()->rules().checkPosition(r.topLeft()));
            r.setSize(win::adjusted_size(this, r.size(), win::size_mode::fixed_width));
            setFrameGeometry(r, geom_mode);
        }

        info->setState(NET::MaxHoriz, NET::Max);
        break;
    }

    case win::maximize_mode::restore: {
        auto restore = frameGeometry();
        // when only partially maximized, restore_geo may not have the other dimension remembered
        if (win::flags(old_mode & win::maximize_mode::vertical)) {
            restore.setTop(restore_geo.top());
            restore.setBottom(restore_geo.bottom());
        }
        if (win::flags(old_mode & win::maximize_mode::horizontal)) {
            restore.setLeft(restore_geo.left());
            restore.setRight(restore_geo.right());
        }

        if (!restore.isValid()) {
            QSize s = QSize(clientArea.width() * 2 / 3, clientArea.height() * 2 / 3);
            if (restore_geo.width() > 0)
                s.setWidth(restore_geo.width());
            if (restore_geo.height() > 0)
                s.setHeight(restore_geo.height());
            plainResize(win::adjusted_size(this, s, win::size_mode::any));
            Placement::self()->placeSmart(this, clientArea);
            restore = frameGeometry();
            if (restore_geo.width() > 0) {
                restore.moveLeft(restore_geo.x());
            }
            if (restore_geo.height() > 0) {
                restore.moveTop(restore_geo.y());
            }
            // relevant for mouse pos calculation, bug #298646
            restore_geo = restore;
        }

        if (m_geometryHints.hasAspect()) {
            restore.setSize(win::adjusted_size(this, restore.size(), win::size_mode::any));
        }

        setFrameGeometry(restore, geom_mode);
        if (!clientArea.contains(restore_geo.center())) {
            // Not restoring to the same screen
            Placement::self()->place(this, clientArea);
        }
        info->setState(NET::States(), NET::Max);
        control()->set_quicktiling(win::quicktiles::none);
        break;
    }

    case win::maximize_mode::full: {
        QRect r(clientArea);
        r.setTopLeft(control()->rules().checkPosition(r.topLeft()));
        r.setSize(win::adjusted_size(this, r.size(), win::size_mode::max));

        if (r.size() != clientArea.size()) {
            // to avoid off-by-one errors...
            if (control()->electric_maximizing() && r.width() < clientArea.width()) {
                r.moveLeft(qMax(clientArea.left(), Cursor::pos().x() - r.width()/2));
                r.moveRight(qMin(clientArea.right(), r.right()));
            } else {
                r.moveCenter(clientArea.center());

                auto const closeHeight = r.height() > 97*clientArea.height()/100;
                auto const closeWidth  = r.width()  > 97*clientArea.width() /100;
                auto const overHeight = r.height() > clientArea.height();
                auto const overWidth  = r.width()  > clientArea.width();

                if (closeWidth || closeHeight) {
                    const QRect screenArea = workspace()->clientArea(ScreenArea, clientArea.center(), desktop());
                    if (closeHeight) {
                        bool tryBottom = false;
                        if (overHeight ||
                            screenArea.top() == clientArea.top())
                            r.setTop(clientArea.top());
                        else
                            tryBottom = true;
                        if (tryBottom &&
                            (overHeight || screenArea.bottom() == clientArea.bottom())) {
                            r.setBottom(clientArea.bottom());
                        }
                    }
                    if (closeWidth) {
                        bool tryLeft = false;
                        if (screenArea.right() == clientArea.right())
                            r.setRight(clientArea.right());
                        else
                            tryLeft = true;
                        if (tryLeft && (overWidth || screenArea.left() == clientArea.left()))
                            r.setLeft(clientArea.left());
                    }
                }
            }

            r.moveTopLeft(control()->rules().checkPosition(r.topLeft()));
        }

        setFrameGeometry(r, geom_mode);

        if (options->electricBorderMaximize() && r.top() == clientArea.top()) {
            control()->set_quicktiling(win::quicktiles::maximize);
        } else {
            control()->set_quicktiling(win::quicktiles::none);
        }

        info->setState(NET::Max, NET::Max);
        break;
    }
    default:
        break;
    }

    updateAllowedActions();
    updateWindowRules(Rules::MaximizeVert|Rules::MaximizeHoriz|Rules::Position|Rules::Size);

    Q_EMIT quicktiling_changed();
}

bool X11Client::userCanSetFullScreen() const
{
    if (!control()->can_fullscreen()) {
        return false;
    }
    return win::is_normal(this) || win::is_dialog(this);
}

void X11Client::setFullScreen(bool set, bool user)
{
    set = control()->rules().checkFullScreen(set);

    auto const wasFullscreen = control()->fullscreen();
    if (wasFullscreen == set) {
        return;
    }

    if (user && !userCanSetFullScreen()) {
        return;
    }

    setShade(win::shade::none);

    if (wasFullscreen) {
        // may cause leave event
        workspace()->updateFocusMousePosition(Cursor::pos());
    } else {
        restore_geometries.fullscreen = frameGeometry();
    }

    control()->set_fullscreen(set);
    if (set) {
        workspace()->raise_window(this);
    }

    StackingUpdatesBlocker blocker1(workspace());
    win::geometry_updates_blocker blocker2(this);

    // active fullscreens get different layer
    workspace()->updateClientLayer(this);

    info->setState(control()->fullscreen() ? NET::FullScreen : NET::States(), NET::FullScreen);
    updateDecoration(false, false);

    if (set) {
        if (info->fullscreenMonitors().isSet()) {
            setFrameGeometry(fullscreenMonitorsArea(info->fullscreenMonitors()));
        } else {
            setFrameGeometry(workspace()->clientArea(FullScreenArea, this));
        }
    } else {
        assert(!restore_geometries.fullscreen.isNull());
        const int currentScreen = screen();
        setFrameGeometry(QRect(restore_geometries.fullscreen.topLeft(),
                               win::adjusted_size(this, restore_geometries.fullscreen.size(),
                                                  win::size_mode::any)));
        if(currentScreen != screen()) {
            workspace()->sendClientToScreen(this, currentScreen);
        }
    }

    updateWindowRules(Rules::Fullscreen | Rules::Position | Rules::Size);

    Q_EMIT clientFullScreenSet(this, set, user);
    Q_EMIT fullScreenChanged();
}


void X11Client::updateFullscreenMonitors(NETFullscreenMonitors topology)
{
    int nscreens = screens()->count();

//    qDebug() << "incoming request with top: " << topology.top << " bottom: " << topology.bottom
//                   << " left: " << topology.left << " right: " << topology.right
//                   << ", we have: " << nscreens << " screens.";

    if (topology.top >= nscreens ||
            topology.bottom >= nscreens ||
            topology.left >= nscreens ||
            topology.right >= nscreens) {
        qCWarning(KWIN_CORE) << "fullscreenMonitors update failed. request higher than number of screens.";
        return;
    }

    info->setFullscreenMonitors(topology);
    if (control()->fullscreen()) {
        setFrameGeometry(fullscreenMonitorsArea(topology));
    }
}

/**
 * Calculates the bounding rectangle defined by the 4 monitor indices indicating the
 * top, bottom, left, and right edges of the window when the fullscreen state is enabled.
 */
QRect X11Client::fullscreenMonitorsArea(NETFullscreenMonitors requestedTopology) const
{
    QRect top, bottom, left, right, total;

    top = screens()->geometry(requestedTopology.top);
    bottom = screens()->geometry(requestedTopology.bottom);
    left = screens()->geometry(requestedTopology.left);
    right = screens()->geometry(requestedTopology.right);
    total = top.united(bottom.united(left.united(right)));

//    qDebug() << "top: " << top << " bottom: " << bottom
//                   << " left: " << left << " right: " << right;
//    qDebug() << "returning rect: " << total;
    return total;
}

static GeometryTip* geometryTip = nullptr;

void X11Client::positionGeometryTip()
{
    assert(win::is_move(this) || win::is_resize(this));

    // Position and Size display
    if (effects && static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::GeometryTip)) {
        // some effect paints this for us
        return;
    }

    if (options->showGeometryTip()) {
        if (!geometryTip) {
            geometryTip = new GeometryTip(&m_geometryHints);
        }

        // position of the frame, size of the window itself
        QRect wgeom(control()->move_resize().geometry);
        wgeom.setWidth(wgeom.width() - (size().width() - clientSize().width()));
        wgeom.setHeight(wgeom.height() - (size().height() - clientSize().height()));

        if (win::shaded(this)) {
            wgeom.setHeight(0);
        }

        geometryTip->setGeometry(wgeom);
        if (!geometryTip->isVisible()) {
            geometryTip->show();
        }
        geometryTip->raise();
    }
}

bool X11Client::doStartMoveResize()
{
    bool has_grab = false;

    // This reportedly improves smoothness of the moveresize operation,
    // something with Enter/LeaveNotify events, looks like XFree performance problem or something *shrug*
    // (https://lists.kde.org/?t=107302193400001&r=1&w=2)
    QRect r = workspace()->clientArea(FullArea, this);

    m_moveResizeGrabWindow.create(r, XCB_WINDOW_CLASS_INPUT_ONLY, 0, nullptr, rootWindow());
    m_moveResizeGrabWindow.map();
    m_moveResizeGrabWindow.raise();

    updateXTime();
    auto const cookie = xcb_grab_pointer_unchecked(connection(), false, m_moveResizeGrabWindow,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, m_moveResizeGrabWindow,
                            Cursor::x11Cursor(control()->move_resize().cursor), xTime());

    ScopedCPointer<xcb_grab_pointer_reply_t> pointerGrab(xcb_grab_pointer_reply(connection(), cookie, nullptr));
    if (!pointerGrab.isNull() && pointerGrab->status == XCB_GRAB_STATUS_SUCCESS) {
        has_grab = true;
    }

    if (!has_grab && grabXKeyboard(frameId()))
        has_grab = move_resize_has_keyboard_grab = true;
    if (!has_grab) {
        // at least one grab is necessary in order to be able to finish move/resize
        m_moveResizeGrabWindow.reset();
        return false;
    }

    return true;
}

void X11Client::leaveMoveResize()
{
    if (needsXWindowMove) {
        // Do the deferred move
        m_frame.move(m_bufferGeometry.topLeft());
        needsXWindowMove = false;
    }

    if (!win::is_resize(this)) {
        // tell the client about it's new final position
        sendSyntheticConfigureNotify();
    }

    if (geometryTip) {
        geometryTip->hide();
        delete geometryTip;
        geometryTip = nullptr;
    }

    if (move_resize_has_keyboard_grab) {
        ungrabXKeyboard();
    }

    move_resize_has_keyboard_grab = false;
    xcb_ungrab_pointer(connection(), xTime());
    m_moveResizeGrabWindow.reset();

    if (m_syncRequest.counter == XCB_NONE) {
        // don't forget to sanitize since the timeout will no more fire
        m_syncRequest.isPending = false;
    }

    delete m_syncRequest.timeout;
    m_syncRequest.timeout = nullptr;
    Toplevel::leaveMoveResize();
}

bool X11Client::isWaitingForMoveResizeSync() const
{
    return m_syncRequest.isPending && win::is_resize(this);
}

void X11Client::doResizeSync()
{
    if (!m_syncRequest.timeout) {
        m_syncRequest.timeout = new QTimer(this);
        connect(m_syncRequest.timeout, &QTimer::timeout,
                this, [this] { win::perform_move_resize(this); });
        m_syncRequest.timeout->setSingleShot(true);
    }

    if (m_syncRequest.counter != XCB_NONE) {
        m_syncRequest.timeout->start(250);
        sendSyncRequest();
    } else {
        // for clients not supporting the XSYNC protocol, we
        // limit the resizes to 30Hz to take pointless load from X11
        // and the client, the mouse is still moved at full speed
        // and no human can control faster resizes anyway
        m_syncRequest.isPending = true;
        m_syncRequest.timeout->start(33);
    }

    auto const more_resize_geo = control()->move_resize().geometry;
    auto const moveResizeClientGeometry = win::frame_rect_to_client_rect(this, more_resize_geo);
    auto const moveResizeBufferGeometry = frameRectToBufferRect(more_resize_geo);

    // According to the Composite extension spec, a window will get a new pixmap allocated each time
    // it is mapped or resized. Given that we redirect frame windows and not client windows, we have
    // to resize the frame window in order to forcefully reallocate offscreen storage. If we don't do
    // this, then we might render partially updated client window. I know, it sucks.
    m_frame.setGeometry(moveResizeBufferGeometry);
    m_wrapper.setGeometry(QRect(win::to_client_pos(this, QPoint()), moveResizeClientGeometry.size()));
    m_client.resize(moveResizeClientGeometry.size());
}

void X11Client::doPerformMoveResize()
{
    if (m_syncRequest.counter == XCB_NONE) {
        // client w/o XSYNC support. allow the next resize event
        // NEVER do this for clients with a valid counter
        // (leads to sync request races in some clients)
        m_syncRequest.isPending = false;
    }
}

/**
 * Returns \a area with the client's strut taken into account.
 *
 * Used from Workspace in updateClientArea.
 */
// TODO move to Workspace?

QRect X11Client::adjustedClientArea(const QRect &desktopArea, const QRect& area) const
{
    auto rect = area;
    NETExtendedStrut str = strut();
    QRect stareaL = QRect(
                        0,
                        str . left_start,
                        str . left_width,
                        str . left_end - str . left_start + 1);
    QRect stareaR = QRect(
                        desktopArea . right() - str . right_width + 1,
                        str . right_start,
                        str . right_width,
                        str . right_end - str . right_start + 1);
    QRect stareaT = QRect(
                        str . top_start,
                        0,
                        str . top_end - str . top_start + 1,
                        str . top_width);
    QRect stareaB = QRect(
                        str . bottom_start,
                        desktopArea . bottom() - str . bottom_width + 1,
                        str . bottom_end - str . bottom_start + 1,
                        str . bottom_width);

    QRect screenarea = workspace()->clientArea(ScreenArea, this);
    // HACK: workarea handling is not xinerama aware, so if this strut
    // reserves place at a xinerama edge that's inside the virtual screen,
    // ignore the strut for workspace setting.
    if (area == QRect(QPoint(0, 0), screens()->displaySize())) {
        if (stareaL.left() < screenarea.left())
            stareaL = QRect();
        if (stareaR.right() > screenarea.right())
            stareaR = QRect();
        if (stareaT.top() < screenarea.top())
            stareaT = QRect();
        if (stareaB.bottom() < screenarea.bottom())
            stareaB = QRect();
    }
    // Handle struts at xinerama edges that are inside the virtual screen.
    // They're given in virtual screen coordinates, make them affect only
    // their xinerama screen.
    stareaL.setLeft(qMax(stareaL.left(), screenarea.left()));
    stareaR.setRight(qMin(stareaR.right(), screenarea.right()));
    stareaT.setTop(qMax(stareaT.top(), screenarea.top()));
    stareaB.setBottom(qMin(stareaB.bottom(), screenarea.bottom()));

    if (stareaL . intersects(area)) {
//        qDebug() << "Moving left of: " << rect << " to " << stareaL.right() + 1;
        rect.setLeft(stareaL . right() + 1);
    }
    if (stareaR . intersects(area)) {
//        qDebug() << "Moving right of: " << rect << " to " << stareaR.left() - 1;
        rect.setRight(stareaR . left() - 1);
    }
    if (stareaT . intersects(area)) {
//        qDebug() << "Moving top of: " <<  << " to " << stareaT.bottom() + 1;
        rect.setTop(stareaT . bottom() + 1);
    }
    if (stareaB . intersects(area)) {
//        qDebug() << "Moving bottom of: " << rect << " to " << stareaB.top() - 1;
        rect.setBottom(stareaB . top() - 1);
    }

    return rect;
}

NETExtendedStrut X11Client::strut() const
{
    NETExtendedStrut ext = info->extendedStrut();
    NETStrut str = info->strut();
    const QSize displaySize = screens()->displaySize();
    if (ext.left_width == 0 && ext.right_width == 0 && ext.top_width == 0 && ext.bottom_width == 0
            && (str.left != 0 || str.right != 0 || str.top != 0 || str.bottom != 0)) {
        // build extended from simple
        if (str.left != 0) {
            ext.left_width = str.left;
            ext.left_start = 0;
            ext.left_end = displaySize.height();
        }
        if (str.right != 0) {
            ext.right_width = str.right;
            ext.right_start = 0;
            ext.right_end = displaySize.height();
        }
        if (str.top != 0) {
            ext.top_width = str.top;
            ext.top_start = 0;
            ext.top_end = displaySize.width();
        }
        if (str.bottom != 0) {
            ext.bottom_width = str.bottom;
            ext.bottom_start = 0;
            ext.bottom_end = displaySize.width();
        }
    }
    return ext;
}

StrutRect X11Client::strutRect(StrutArea area) const
{
    // Not valid
    assert(area != StrutAreaAll);

    auto const displaySize = screens()->displaySize();
    NETExtendedStrut strutArea = strut();

    switch(area) {
    case StrutAreaTop:
        if (strutArea.top_width != 0)
            return StrutRect(QRect(
                                 strutArea.top_start, 0,
                                 strutArea.top_end - strutArea.top_start, strutArea.top_width
                             ), StrutAreaTop);
        break;
    case StrutAreaRight:
        if (strutArea.right_width != 0)
            return StrutRect(QRect(
                                 displaySize.width() - strutArea.right_width, strutArea.right_start,
                                 strutArea.right_width, strutArea.right_end - strutArea.right_start
                             ), StrutAreaRight);
        break;
    case StrutAreaBottom:
        if (strutArea.bottom_width != 0)
            return StrutRect(QRect(
                                 strutArea.bottom_start, displaySize.height() - strutArea.bottom_width,
                                 strutArea.bottom_end - strutArea.bottom_start, strutArea.bottom_width
                             ), StrutAreaBottom);
        break;
    case StrutAreaLeft:
        if (strutArea.left_width != 0)
            return StrutRect(QRect(
                                 0, strutArea.left_start,
                                 strutArea.left_width, strutArea.left_end - strutArea.left_start
                             ), StrutAreaLeft);
        break;
    default:
        // Not valid
        abort();
    }

    return StrutRect();
}

StrutRects X11Client::strutRects() const
{
    StrutRects region;
    region += strutRect(StrutAreaTop);
    region += strutRect(StrutAreaRight);
    region += strutRect(StrutAreaBottom);
    region += strutRect(StrutAreaLeft);
    return region;
}

bool X11Client::hasStrut() const
{
    NETExtendedStrut ext = strut();
    if (ext.left_width == 0 && ext.right_width == 0 && ext.top_width == 0 && ext.bottom_width == 0) {
        return false;
    }
    return true;
}

bool X11Client::hasOffscreenXineramaStrut() const
{
    // Get strut as a QRegion
    QRegion region;
    region += strutRect(StrutAreaTop);
    region += strutRect(StrutAreaRight);
    region += strutRect(StrutAreaBottom);
    region += strutRect(StrutAreaLeft);

    // Remove all visible areas so that only the invisible remain
    for (int i = 0; i < screens()->count(); i ++) {
        region -= screens()->geometry(i);
    }

    // If there's anything left then we have an offscreen strut
    return !region.isEmpty();
}

void X11Client::applyWindowRules()
{
    Toplevel::applyWindowRules();
    setBlockingCompositing(info->isBlockingCompositing());
}

void X11Client::damageNotifyEvent()
{
    if (m_syncRequest.isPending && win::is_resize(this)) {
        Q_EMIT damaged(this, QRect());
        m_isDamaged = true;
        return;
    }

    if (!readyForPainting()) {
        // avoid "setReadyForPainting()" function calling overhead
        if (m_syncRequest.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            setReadyForPainting();
            win::setup_wayland_plasma_management(this);
        }
    }

    Toplevel::damageNotifyEvent();
}

void X11Client::updateWindowPixmap()
{
    if (effectWindow() && effectWindow()->sceneWindow()) {
        effectWindow()->sceneWindow()->updatePixmap();
    }
}

bool X11Client::isShown(bool shaded_is_shown) const
{
    return !control()->minimized() && (!win::shaded(this) || shaded_is_shown) && !hidden;
}

}
