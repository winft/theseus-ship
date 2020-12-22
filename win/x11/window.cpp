/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "activity.h"
#include "client.h"
#include "deco.h"
#include "geo.h"
#include "hide.h"
#include "meta.h"
#include "transient.h"

#include "win/deco.h"
#include "win/remnant.h"
#include "win/rules.h"

#include "decorations/window.h"
#include "geometrytip.h"
#include "rules/rules.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

#include <KDecoration2/DecoratedClient>

namespace KWin::win::x11
{

window::window()
    : Toplevel(new x11::transient(this))
    , motif_hints(atoms->motif_wm_hints)
{
    // So that decorations don't start with size being (0,0).
    set_frame_geometry(QRect(0, 0, 100, 100));
}

window::~window()
{
    if (kill_helper_pid && !::kill(kill_helper_pid, 0)) {
        // The process is still alive.
        ::kill(kill_helper_pid, SIGTERM);
        kill_helper_pid = 0;
    }

    if (sync_request.alarm != XCB_NONE) {
        xcb_sync_destroy_alarm(connection(), sync_request.alarm);
    }

    assert(!control || !control->move_resize().enabled);
    assert(xcb_windows.client == XCB_WINDOW_NONE);
    assert(xcb_windows.wrapper == XCB_WINDOW_NONE);
    assert(xcb_windows.frame == XCB_WINDOW_NONE);
}

bool window::isClient() const
{
    return true;
}

xcb_window_t window::frameId() const
{
    return xcb_windows.frame;
}

void window::updateCaption()
{
    set_caption(this, caption.normal, true);
}

bool window::belongsToSameApplication(Toplevel const* other, win::same_client_check checks) const
{
    auto c2 = dynamic_cast<const window*>(other);
    if (!c2) {
        return false;
    }
    return belong_to_same_application(this, c2, checks);
}

/**
 * Returns whether the window provides context help or not. If it does,
 * you should show a help menu item or a help button like '?' and call
 * contextHelp() if this is invoked.
 */
bool window::providesContextHelp() const
{
    return info->supportsProtocol(NET::ContextHelpProtocol);
}

/**
 * Invokes context help on the window. Only works if the window
 * actually provides context help.
 */
void window::showContextHelp()
{
    if (info->supportsProtocol(NET::ContextHelpProtocol)) {
        send_client_message(xcb_window(), atoms->wm_protocols, atoms->net_wm_context_help);
    }
}

bool window::noBorder() const
{
    return user_no_border || control->fullscreen();
}

void window::setNoBorder(bool set)
{
    if (!userCanSetNoBorder()) {
        return;
    }

    set = control->rules().checkNoBorder(set);
    if (user_no_border == set) {
        return;
    }

    user_no_border = set;
    updateDecoration(true, false);
    updateWindowRules(Rules::NoBorder);
}

bool window::userCanSetNoBorder() const
{
    if (!client_frame_extents.isNull()) {
        // CSD allow no change by user.
        return false;
    }
    return !control->fullscreen() && !win::shaded(this);
}

void window::checkNoBorder()
{
    setNoBorder(app_no_border);
}

bool window::wantsShadowToBeRendered() const
{
    return !control->fullscreen() && maximizeMode() != win::maximize_mode::full;
}

QSize window::resizeIncrements() const
{
    return geometry_hints.resizeIncrements();
}

static Xcb::Window shape_helper_window(XCB_WINDOW_NONE);

void window::cleanupX11()
{
    shape_helper_window.reset();
}

void window::update_input_shape()
{
    if (hidden_preview(this)) {
        // Sets it to none, don't change.
        return;
    }

    if (!Xcb::Extensions::self()->isShapeInputAvailable()) {
        return;
    }
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

    shape_helper_window.resize(geometries.buffer.size());
    auto con = connection();
    auto const client_pos = win::to_client_pos(this, QPoint());

    xcb_shape_combine(con,
                      XCB_SHAPE_SO_SET,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_BOUNDING,
                      shape_helper_window,
                      0,
                      0,
                      frameId());
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_SUBTRACT,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_BOUNDING,
                      shape_helper_window,
                      client_pos.x(),
                      client_pos.y(),
                      xcb_window());
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_UNION,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_INPUT,
                      shape_helper_window,
                      client_pos.x(),
                      client_pos.y(),
                      xcb_window());
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_SET,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_INPUT,
                      frameId(),
                      0,
                      0,
                      shape_helper_window);
}

QRect window::iconGeometry() const
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

bool window::setupCompositing(bool add_full_damage)
{
    if (!Toplevel::setupCompositing(add_full_damage)) {
        return false;
    }

    // for internalKeep()
    update_visibility(this);

    return true;
}

void window::finishCompositing(ReleaseReason releaseReason)
{
    Toplevel::finishCompositing(releaseReason);

    // for safety in case KWin is just resizing the window
    control->reset_have_resize_effect();
}

void window::setBlockingCompositing(bool block)
{
    auto const usedToBlock = blocks_compositing;
    blocks_compositing
        = control->rules().checkBlockCompositing(block && options->windowsBlockCompositing());

    if (usedToBlock != blocks_compositing) {
        Q_EMIT blockingCompositingChanged(blocks_compositing ? this : nullptr);
    }
}

void window::damageNotifyEvent()
{
    if (sync_request.isPending && win::is_resize(this)) {
        Q_EMIT damaged(this, QRect());
        m_isDamaged = true;
        return;
    }

    if (!readyForPainting()) {
        // avoid "setReadyForPainting()" function calling overhead
        if (sync_request.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            setReadyForPainting();
            win::setup_wayland_plasma_management(this);
        }
    }

    Toplevel::damageNotifyEvent();
}

void window::release_window(bool on_shutdown)
{
    Q_ASSERT(!deleting);
    deleting = true;

#ifdef KWIN_BUILD_TABBOX
    auto tabbox = TabBox::TabBox::self();
    if (tabbox->isDisplayed() && tabbox->currentClient() == this) {
        tabbox->nextPrev(true);
    }
#endif

    control->destroy_wayland_management();

    Toplevel* del = nullptr;
    if (!on_shutdown) {
        del = create_remnant(this);
    }

    if (control->move_resize().enabled) {
        Q_EMIT clientFinishUserMovedResized(this);
    }

    Q_EMIT windowClosed(this, del);
    finishCompositing();

    // Remove ForceTemporarily rules
    RuleBook::self()->discardUsed(this, true);

    StackingUpdatesBlocker blocker(workspace());

    if (control->move_resize().enabled) {
        leaveMoveResize();
    }

    win::finish_rules(this);
    control->block_geometry_updates();

    if (isOnCurrentDesktop() && isShown(true)) {
        addWorkspaceRepaint(win::visible_rect(this));
    }

    // Grab X during the release to make removing of properties, setting to withdrawn state
    // and repareting to root an atomic operation
    // (https://lists.kde.org/?l=kde-devel&m=116448102901184&w=2)
    grabXServer();
    export_mapping_state(this, XCB_ICCCM_WM_STATE_WITHDRAWN);

    // So that it's not considered visible anymore (can't use hideClient(), it would set flags)
    hidden = true;

    if (!on_shutdown) {
        workspace()->clientHidden(this);
    }

    // Destroying decoration would cause ugly visual effect
    xcb_windows.frame.unmap();

    control->destroy_decoration();
    clean_grouping(this);

    if (!on_shutdown) {
        workspace()->removeClient(this);
        // Only when the window is being unmapped, not when closing down KWin (NETWM
        // sections 5.5,5.7)
        info->setDesktop(0);
        info->setState(NET::States(), info->state()); // Reset all state flags
    }

    xcb_windows.client.deleteProperty(atoms->kde_net_wm_user_creation_time);
    xcb_windows.client.deleteProperty(atoms->net_frame_extents);
    xcb_windows.client.deleteProperty(atoms->kde_net_wm_frame_strut);

    xcb_windows.client.reparent(rootWindow(), geometries.buffer.x(), geometries.buffer.y());
    xcb_change_save_set(connection(), XCB_SET_MODE_DELETE, xcb_windows.client);
    xcb_windows.client.selectInput(XCB_EVENT_MASK_NO_EVENT);

    if (on_shutdown) {
        // Map the window, so it can be found after another WM is started
        xcb_windows.client.map();
        // TODO: Preserve minimized, shaded etc. state?
    } else {
        // Make sure it's not mapped if the app unmapped it (#65279). The app
        // may do map+unmap before we initially map the window by calling rawShow() from manage().
        xcb_windows.client.unmap();
    }

    xcb_windows.client.reset();
    xcb_windows.wrapper.reset();
    xcb_windows.frame.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    control->unblock_geometry_updates();

    if (!on_shutdown) {
        disownDataPassedToDeleted();
        del->remnant()->unref();
    }

    delete this;
    ungrabXServer();
}

void window::applyWindowRules()
{
    Toplevel::applyWindowRules();
    setBlockingCompositing(info->isBlockingCompositing());
}

void window::updateWindowRules(Rules::Types selection)
{
    if (!m_managed) {
        // not fully setup yet
        return;
    }
    Toplevel::updateWindowRules(selection);
}

/**
 * Like release(), but window is already destroyed (for example app closed it).
 */
void window::destroy()
{
    assert(!deleting);
    deleting = true;

#ifdef KWIN_BUILD_TABBOX
    auto tabbox = TabBox::TabBox::self();
    if (tabbox && tabbox->isDisplayed() && tabbox->currentClient() == this) {
        tabbox->nextPrev(true);
    }
#endif

    control->destroy_wayland_management();

    auto del = create_remnant(this);

    if (control->move_resize().enabled) {
        Q_EMIT clientFinishUserMovedResized(this);
    }
    Q_EMIT windowClosed(this, del);

    finishCompositing(ReleaseReason::Destroyed);

    // Remove ForceTemporarily rules
    RuleBook::self()->discardUsed(this, true);

    StackingUpdatesBlocker blocker(workspace());
    if (control->move_resize().enabled) {
        leaveMoveResize();
    }

    win::finish_rules(this);
    control->block_geometry_updates();

    if (isOnCurrentDesktop() && isShown(true)) {
        addWorkspaceRepaint(win::visible_rect(this));
    }

    // So that it's not considered visible anymore
    hidden = true;

    workspace()->clientHidden(this);
    control->destroy_decoration();
    clean_grouping(this);
    workspace()->removeClient(this);

    // invalidate
    xcb_windows.client.reset();
    xcb_windows.wrapper.reset();
    xcb_windows.frame.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    control->unblock_geometry_updates();
    disownDataPassedToDeleted();
    del->remnant()->unref();
    delete this;
}

void window::closeWindow()
{
    if (!isCloseable()) {
        return;
    }

    // Update user time, because the window may create a confirming dialog.
    update_user_time(this);

    if (info->supportsProtocol(NET::DeleteWindowProtocol)) {
        send_client_message(xcb_window(), atoms->wm_protocols, atoms->wm_delete_window);
        ping(this);
    } else {
        // Client will not react on wm_delete_window. We have not choice
        // but destroy his connection to the XServer.
        killWindow();
    }
}

QSize window::clientSize() const
{
    return geometries.client.size();
}

QSize window::sizeForClientSize(QSize const& wsize, win::size_mode mode, bool noframe) const
{
    return size_for_client_size(this, wsize, mode, noframe);
}

QSize window::minSize() const
{
    return control->rules().checkMinSize(geometry_hints.minSize());
}

QSize window::maxSize() const
{
    return control->rules().checkMaxSize(geometry_hints.maxSize());
}

QSize window::basicUnit() const
{
    return geometry_hints.resizeIncrements();
}

bool window::isCloseable() const
{
    return control->rules().checkCloseable(motif_hints.close() && !win::is_special_window(this));
}

bool window::isMaximizable() const
{
    if (!isResizable() || win::is_toolbar(this)) {
        // SELI isToolbar() ?
        return false;
    }
    if (control->rules().checkMaximize(win::maximize_mode::restore) == win::maximize_mode::restore
        && control->rules().checkMaximize(win::maximize_mode::full)
            != win::maximize_mode::restore) {
        return true;
    }
    return false;
}

bool window::isMinimizable() const
{
    if (win::is_special_window(this) && !isTransient()) {
        return false;
    }
    if (!control->rules().checkMinimize(true)) {
        return false;
    }

    if (isTransient()) {
        // #66868 - Let other xmms windows be minimized when the mainwindow is minimized
        auto shown_main_window = false;
        for (auto const& lead : transient()->leads())
            if (lead->isShown(true)) {
                shown_main_window = true;
            }
        if (!shown_main_window) {
            return true;
        }
    }

    if (!win::wants_tab_focus(this)) {
        return false;
    }
    return true;
}

bool window::isMovable() const
{
    if (!info->hasNETSupport() && !motif_hints.move()) {
        return false;
    }
    if (control->fullscreen()) {
        return false;
    }
    if (win::is_special_window(this) && !win::is_splash(this) && !win::is_toolbar(this)) {
        // allow moving of splashscreens :)
        return false;
    }
    if (control->rules().checkPosition(invalidPoint) != invalidPoint) {
        // forced position
        return false;
    }
    return true;
}

bool window::isMovableAcrossScreens() const
{
    if (!info->hasNETSupport() && !motif_hints.move()) {
        return false;
    }
    if (win::is_special_window(this) && !win::is_splash(this) && !win::is_toolbar(this)) {
        // allow moving of splashscreens :)
        return false;
    }
    if (control->rules().checkPosition(invalidPoint) != invalidPoint) {
        // forced position
        return false;
    }
    return true;
}

bool window::isResizable() const
{
    if (!info->hasNETSupport() && !motif_hints.resize()) {
        return false;
    }
    if (control->fullscreen()) {
        return false;
    }
    if (win::is_special_window(this) || win::is_splash(this) || win::is_toolbar(this)) {
        return false;
    }
    if (control->rules().checkSize(QSize()).isValid()) {
        // forced size
        return false;
    }

    auto const mode = control->move_resize().contact;

    // TODO: we could just check with & on top and left.
    if ((mode == win::position::top || mode == win::position::top_left
         || mode == win::position::top_right || mode == win::position::left
         || mode == win::position::bottom_left)
        && control->rules().checkPosition(invalidPoint) != invalidPoint) {
        return false;
    }

    auto min = minSize();
    auto max = maxSize();

    return min.width() < max.width() || min.height() < max.height();
}

bool window::groupTransient() const
{
    return static_cast<win::x11::transient*>(transient())->lead_id == rootWindow();
}

void window::takeFocus()
{
    if (control->rules().checkAcceptFocus(info->input())) {
        xcb_windows.client.focus();
    } else {
        // window cannot take input, at least withdraw urgency
        win::set_demands_attention(this, false);
    }

    if (info->supportsProtocol(NET::TakeFocusProtocol)) {
        updateXTime();
        send_client_message(xcb_window(), atoms->wm_protocols, atoms->wm_take_focus);
    }

    workspace()->setShouldGetFocus(this);
    auto breakShowingDesktop = !control->keep_above();

    if (breakShowingDesktop) {
        for (auto const& c : group()->members()) {
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

xcb_timestamp_t window::userTime() const
{
    xcb_timestamp_t time = user_time;
    if (time == 0) {
        // Doesn't want focus after showing.
        return 0;
    }

    assert(group() != nullptr);

    if (time == -1U
        || (group()->userTime() != -1U && NET::timestampCompare(group()->userTime(), time) > 0)) {
        time = group()->userTime();
    }
    return time;
}

void window::doSetActive()
{
    // Demand attention again if it's still urgent.
    update_urgency(this);
    info->setState(control->active() ? NET::Focused : NET::States(), NET::Focused);
}

bool window::userCanSetFullScreen() const
{
    if (!control->can_fullscreen()) {
        return false;
    }
    return win::is_normal(this) || win::is_dialog(this);
}

bool window::wantsInput() const
{
    return control->rules().checkAcceptFocus(acceptsFocus()
                                             || info->supportsProtocol(NET::TakeFocusProtocol));
}

bool window::acceptsFocus() const
{
    return info->input();
}

bool window::isShown(bool shaded_is_shown) const
{
    return !control->minimized() && (!win::shaded(this) || shaded_is_shown) && !hidden;
}

bool window::isHiddenInternal() const
{
    return hidden;
}

win::shade window::shadeMode() const
{
    return shade_mode;
}

bool window::isShadeable() const
{
    return !win::is_special_window(this) && !noBorder()
        && (control->rules().checkShade(win::shade::normal)
            != control->rules().checkShade(win::shade::none));
}

void window::setShade(win::shade mode)
{
    set_shade(this, mode);
}

void window::shade_hover()
{
    setShade(win::shade::hover);
    cancel_shade_hover_timer();
}

void window::shade_unhover()
{
    setShade(win::shade::normal);
    cancel_shade_hover_timer();
}

void window::cancel_shade_hover_timer()
{
    delete shade_hover_timer;
    shade_hover_timer = nullptr;
}

void window::toggleShade()
{
    // If the mode is win::shade::hover or win::shade::active, cancel shade too
    setShade(shade_mode == win::shade::none ? win::shade::normal : win::shade::none);
}

bool window::performMouseCommand(Options::MouseCommand command, QPoint const& globalPos)
{
    return x11::perform_mouse_command(this, command, globalPos);
}

void window::setShortcutInternal()
{
    updateCaption();
#if 0
    workspace()->clientShortcutUpdated(this);
#else
    // Workaround for kwin<->kglobalaccel deadlock, when KWin has X grab and the kded
    // kglobalaccel module tries to create the key grab. KWin should preferably grab
    // they keys itself anyway :(.
    QTimer::singleShot(0, this, std::bind(&Workspace::clientShortcutUpdated, workspace(), this));
#endif
}

void window::hideClient(bool hide)
{
    if (hidden == hide) {
        return;
    }
    hidden = hide;
    update_visibility(this);
}

void window::setClientShown(bool shown)
{
    set_client_shown(this, shown);
}

QRect window::bufferGeometry() const
{
    return geometries.buffer;
}

void window::addDamage(QRegion const& damage)
{
    if (!ready_for_painting) {
        // avoid "setReadyForPainting()" function calling overhead
        if (sync_request.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            setReadyForPainting();
            win::setup_wayland_plasma_management(this);
        }
    }
    Toplevel::addDamage(damage);
}

maximize_mode window::maximizeMode() const
{
    return max_mode;
}

/**
 * Reimplemented to inform the client about the new window position.
 */
void window::setFrameGeometry(QRect const& rect, force_geometry force)
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
            geometries.client = win::frame_rect_to_client_rect(this, frameGeometry);
            frameGeometry.setHeight(win::top_border(this) + win::bottom_border(this));
        }
    } else {
        geometries.client = win::frame_rect_to_client_rect(this, frameGeometry);
    }

    auto const bufferGeometry = frame_rect_to_buffer_rect(this, frameGeometry);
    if (!control->geometry_updates_blocked()
        && frameGeometry != control->rules().checkGeometry(frameGeometry)) {
        qCDebug(KWIN_CORE) << "forced geometry fail:" << frameGeometry << ":"
                           << control->rules().checkGeometry(frameGeometry);
    }

    set_frame_geometry(frameGeometry);
    if (force == win::force_geometry::no && geometries.buffer == bufferGeometry
        && control->pending_geometry_update() == win::pending_geometry::none) {
        return;
    }

    geometries.buffer = bufferGeometry;

    if (control->geometry_updates_blocked()) {
        if (control->pending_geometry_update() == win::pending_geometry::forced) {
            // maximum, nothing needed
        } else if (force == win::force_geometry::yes) {
            control->set_pending_geometry_update(win::pending_geometry::forced);
        } else {
            control->set_pending_geometry_update(win::pending_geometry::normal);
        }
        return;
    }

    update_server_geometry(this);
    updateWindowRules(static_cast<Rules::Types>(Rules::Position | Rules::Size));

    // keep track of old maximize mode
    // to detect changes
    screens()->setCurrent(this);
    workspace()->updateStackingOrder();

    // Need to regenerate decoration pixmaps when the buffer size is changed.
    if (control->buffer_geometry_before_update_blocking().size() != geometries.buffer.size()) {
        discardWindowPixmap();
    }

    Q_EMIT geometryShapeChanged(this, control->frame_geometry_before_update_blocking());
    win::add_repaint_during_geometry_updates(this);
    control->update_geometry_before_update_blocking();

    // TODO: this signal is emitted too often
    Q_EMIT geometryChanged();
}

static bool changeMaximizeRecursion = false;

void window::changeMaximize(bool horizontal, bool vertical, bool adjust)
{
    if (changeMaximizeRecursion) {
        return;
    }

    if (!isResizable() || win::is_toolbar(this)) {
        // SELI isToolbar() ?
        return;
    }

    QRect clientArea;
    if (control->electric_maximizing()) {
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
    if (geometry_hints.hasAspect()
        && (max_mode == win::maximize_mode::vertical || max_mode == win::maximize_mode::horizontal)
        && control->rules().checkStrictGeometry(true)) {
        // fixed aspect; on dimensional maximization obey aspect
        auto const minAspect = geometry_hints.minAspect();
        auto const maxAspect = geometry_hints.maxAspect();

        if (max_mode == win::maximize_mode::vertical
            || win::flags(old_mode & win::maximize_mode::vertical)) {
            // use doubles, because the values can be MAX_INT
            double const fx = minAspect.width();
            double const fy = maxAspect.height();

            if (fx * clientArea.height() / fy > clientArea.width()) {
                // too big
                max_mode = win::flags(old_mode & win::maximize_mode::horizontal)
                    ? win::maximize_mode::restore
                    : win::maximize_mode::full;
            }
        } else {
            // max_mode == win::maximize_mode::horizontal
            double const fx = maxAspect.width();
            double const fy = minAspect.height();
            if (fy * clientArea.width() / fx > clientArea.height()) {
                // too big
                max_mode = win::flags(old_mode & win::maximize_mode::vertical)
                    ? win::maximize_mode::restore
                    : win::maximize_mode::full;
            }
        }
    }

    max_mode = control->rules().checkMaximize(max_mode);

    if (!adjust && max_mode == old_mode) {
        return;
    }

    win::geometry_updates_blocker blocker(this);

    // maximing one way and unmaximizing the other way shouldn't happen,
    // so restore first and then maximize the other way
    if ((old_mode == win::maximize_mode::vertical && max_mode == win::maximize_mode::horizontal)
        || (old_mode == win::maximize_mode::horizontal
            && max_mode == win::maximize_mode::vertical)) {
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

    if (control->quicktiling() == win::quicktiles::none) {
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
    if (win::decoration(this) && win::decoration(this)->client()
        && !(options->borderlessMaximizedWindows() && max_mode == win::maximize_mode::full)) {
        changeMaximizeRecursion = true;
        auto const c = win::decoration(this)->client().toStrongRef().data();

        if ((max_mode & win::maximize_mode::vertical)
            != (old_mode & win::maximize_mode::vertical)) {
            Q_EMIT c->maximizedVerticallyChanged(
                win::flags(max_mode & win::maximize_mode::vertical));
        }
        if ((max_mode & win::maximize_mode::horizontal)
            != (old_mode & win::maximize_mode::horizontal)) {
            Q_EMIT c->maximizedHorizontallyChanged(
                win::flags(max_mode & win::maximize_mode::horizontal));
        }
        if ((max_mode == win::maximize_mode::full) != (old_mode == win::maximize_mode::full)) {
            Q_EMIT c->maximizedChanged(win::flags(max_mode & win::maximize_mode::full));
        }

        changeMaximizeRecursion = false;
    }

    if (options->borderlessMaximizedWindows()) {
        // triggers a maximize change.
        // The next setNoBorder interation will exit since there's no change but the first recursion
        // pullutes the restore geometry
        changeMaximizeRecursion = true;
        setNoBorder(control->rules().checkNoBorder(
            app_no_border || (motif_hints.hasDecoration() && motif_hints.noBorder())
            || max_mode == win::maximize_mode::full));
        changeMaximizeRecursion = false;
    }

    auto const geom_mode
        = win::decoration(this) ? win::force_geometry::yes : win::force_geometry::no;

    // Conditional quick tiling exit points
    if (control->quicktiling() != win::quicktiles::none) {
        if (old_mode == win::maximize_mode::full
            && !clientArea.contains(restore_geometries.maximize.center())) {
            // Not restoring on the same screen
            // TODO: The following doesn't work for some reason
            // quick_tile_mode = win::quicktiles::none; // And exit quick tile mode manually
        } else if ((old_mode == win::maximize_mode::vertical
                    && max_mode == win::maximize_mode::restore)
                   || (old_mode == win::maximize_mode::full
                       && max_mode == win::maximize_mode::horizontal)) {
            // Modifying geometry of a tiled window
            // Exit quick tile mode without restoring geometry
            control->set_quicktiling(win::quicktiles::none);
        }
    }

    auto& restore_geo = restore_geometries.maximize;

    switch (max_mode) {
    case win::maximize_mode::vertical: {
        if (win::flags(old_mode & win::maximize_mode::horizontal)) {
            // actually restoring from win::maximize_mode::full
            if (restore_geo.width() == 0 || !clientArea.contains(restore_geo.center())) {
                // needs placement
                plain_resize(this,
                             win::adjusted_size(this,
                                                QSize(size().width() * 2 / 3, clientArea.height()),
                                                win::size_mode::fixed_height),
                             geom_mode);
                Placement::self()->placeSmart(this, clientArea);
            } else {
                setFrameGeometry(
                    QRect(QPoint(restore_geo.x(), clientArea.top()),
                          win::adjusted_size(this,
                                             QSize(restore_geo.width(), clientArea.height()),
                                             win::size_mode::fixed_height)),
                    geom_mode);
            }
        } else {
            QRect r(pos().x(), clientArea.top(), size().width(), clientArea.height());
            r.setTopLeft(control->rules().checkPosition(r.topLeft()));
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
                plain_resize(this,
                             win::adjusted_size(this,
                                                QSize(clientArea.width(), size().height() * 2 / 3),
                                                win::size_mode::fixed_width),
                             geom_mode);
                Placement::self()->placeSmart(this, clientArea);
            } else {
                setFrameGeometry(
                    QRect(QPoint(clientArea.left(), restore_geo.y()),
                          win::adjusted_size(this,
                                             QSize(clientArea.width(), restore_geo.height()),
                                             win::size_mode::fixed_width)),
                    geom_mode);
            }
        } else {
            QRect r(clientArea.left(), pos().y(), clientArea.width(), size().height());
            r.setTopLeft(control->rules().checkPosition(r.topLeft()));
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
            plain_resize(this, win::adjusted_size(this, s, win::size_mode::any));
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

        if (geometry_hints.hasAspect()) {
            restore.setSize(win::adjusted_size(this, restore.size(), win::size_mode::any));
        }

        setFrameGeometry(restore, geom_mode);
        if (!clientArea.contains(restore_geo.center())) {
            // Not restoring to the same screen
            Placement::self()->place(this, clientArea);
        }
        info->setState(NET::States(), NET::Max);
        control->set_quicktiling(win::quicktiles::none);
        break;
    }

    case win::maximize_mode::full: {
        QRect r(clientArea);
        r.setTopLeft(control->rules().checkPosition(r.topLeft()));
        r.setSize(win::adjusted_size(this, r.size(), win::size_mode::max));

        if (r.size() != clientArea.size()) {
            // to avoid off-by-one errors...
            if (control->electric_maximizing() && r.width() < clientArea.width()) {
                r.moveLeft(qMax(clientArea.left(), Cursor::pos().x() - r.width() / 2));
                r.moveRight(qMin(clientArea.right(), r.right()));
            } else {
                r.moveCenter(clientArea.center());

                auto const closeHeight = r.height() > 97 * clientArea.height() / 100;
                auto const closeWidth = r.width() > 97 * clientArea.width() / 100;
                auto const overHeight = r.height() > clientArea.height();
                auto const overWidth = r.width() > clientArea.width();

                if (closeWidth || closeHeight) {
                    const QRect screenArea
                        = workspace()->clientArea(ScreenArea, clientArea.center(), desktop());
                    if (closeHeight) {
                        bool tryBottom = false;
                        if (overHeight || screenArea.top() == clientArea.top())
                            r.setTop(clientArea.top());
                        else
                            tryBottom = true;
                        if (tryBottom
                            && (overHeight || screenArea.bottom() == clientArea.bottom())) {
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

            r.moveTopLeft(control->rules().checkPosition(r.topLeft()));
        }

        setFrameGeometry(r, geom_mode);

        if (options->electricBorderMaximize() && r.top() == clientArea.top()) {
            control->set_quicktiling(win::quicktiles::maximize);
        } else {
            control->set_quicktiling(win::quicktiles::none);
        }

        info->setState(NET::Max, NET::Max);
        break;
    }
    default:
        break;
    }

    update_allowed_actions(this);
    updateWindowRules(static_cast<Rules::Types>(Rules::MaximizeVert | Rules::MaximizeHoriz
                                                | Rules::Position | Rules::Size));

    Q_EMIT quicktiling_changed();
}

void window::setFullScreen(bool set, bool user)
{
    set = control->rules().checkFullScreen(set);

    auto const wasFullscreen = control->fullscreen();
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

    control->set_fullscreen(set);
    if (set) {
        workspace()->raise_window(this);
    }

    StackingUpdatesBlocker blocker1(workspace());
    win::geometry_updates_blocker blocker2(this);

    // active fullscreens get different layer
    workspace()->updateClientLayer(this);

    info->setState(control->fullscreen() ? NET::FullScreen : NET::States(), NET::FullScreen);
    updateDecoration(false, false);

    if (set) {
        if (info->fullscreenMonitors().isSet()) {
            setFrameGeometry(fullscreen_monitors_area(info->fullscreenMonitors()));
        } else {
            setFrameGeometry(workspace()->clientArea(FullScreenArea, this));
        }
    } else {
        assert(!restore_geometries.fullscreen.isNull());
        const int currentScreen = screen();
        setFrameGeometry(QRect(
            restore_geometries.fullscreen.topLeft(),
            win::adjusted_size(this, restore_geometries.fullscreen.size(), win::size_mode::any)));
        if (currentScreen != screen()) {
            workspace()->sendClientToScreen(this, currentScreen);
        }
    }

    updateWindowRules(static_cast<Rules::Types>(Rules::Fullscreen | Rules::Position | Rules::Size));

    Q_EMIT clientFullScreenSet(this, set, user);
    Q_EMIT fullScreenChanged();
}

static GeometryTip* geometryTip = nullptr;

void window::reposition_geometry_tip()
{
    assert(win::is_move(this) || win::is_resize(this));

    // Position and Size display
    if (effects && static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::GeometryTip)) {
        // some effect paints this for us
        return;
    }

    if (options->showGeometryTip()) {
        if (!geometryTip) {
            geometryTip = new GeometryTip(&geometry_hints);
        }

        // position of the frame, size of the window itself
        QRect wgeom(control->move_resize().geometry);
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

bool window::belongsToDesktop() const
{
    for (auto const& member : group()->members()) {
        if (win::is_desktop(member)) {
            return true;
        }
    }
    return false;
}

void window::doSetDesktop([[maybe_unused]] int desktop, [[maybe_unused]] int was_desk)
{
    update_visibility(this);
}

const Group* window::group() const
{
    return in_group;
}

Group* window::group()
{
    return in_group;
}

void window::checkTransient(Toplevel* window)
{
    auto id = window->xcb_window();
    if (x11_transient(this)->original_lead_id != id) {
        return;
    }
    id = verify_transient_for(this, id, true);
    set_transient_lead(this, id);
}

Toplevel* window::findModal()
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

void window::layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
{
    layout_decoration_rects(this, left, top, right, bottom);
}

void window::updateDecoration(bool check_workspace_pos, bool force)
{
    update_decoration(this, check_workspace_pos, force);
}

void window::updateColorScheme()
{
}

QStringList window::activities() const
{
    return x11::activities(this);
}

void window::setOnActivities(QStringList newActivitiesList)
{
    set_on_activities(this, newActivitiesList);
}

void window::setOnAllActivities(bool on)
{
    set_on_all_activities(this, on);
}

void window::blockActivityUpdates(bool b)
{
    block_activity_updates(this, b);
}

bool window::hasStrut() const
{
    NETExtendedStrut ext = strut(this);
    if (ext.left_width == 0 && ext.right_width == 0 && ext.top_width == 0
        && ext.bottom_width == 0) {
        return false;
    }
    return true;
}

void window::resizeWithChecks(QSize const& size, force_geometry force)
{
    resize_with_checks(this, size, XCB_GRAVITY_BIT_FORGET, force);
}

/**
 * Kills the window via XKill
 */
void window::killWindow()
{
    qCDebug(KWIN_CORE) << "window::killWindow():" << win::caption(this);
    kill_process(this, false);

    // Always kill this client at the server
    xcb_windows.client.kill();

    destroy();
}

void window::debug(QDebug& stream) const
{
    stream.nospace();
    print<QDebug>(stream);
}

void window::doMinimize()
{
    update_visibility(this);
    update_allowed_actions(this);
    workspace()->updateMinimizedOfTransients(this);
}

void window::showOnScreenEdge()
{
    disconnect(connections.edge_remove);

    hideClient(false);
    win::set_keep_below(this, false);
    xcb_delete_property(connection(), xcb_window(), atoms->kde_screen_edge_show);
}

bool window::doStartMoveResize()
{
    bool has_grab = false;

    // This reportedly improves smoothness of the moveresize operation,
    // something with Enter/LeaveNotify events, looks like XFree performance problem or something
    // *shrug* (https://lists.kde.org/?t=107302193400001&r=1&w=2)
    QRect r = workspace()->clientArea(FullArea, this);

    xcb_windows.grab.create(r, XCB_WINDOW_CLASS_INPUT_ONLY, 0, nullptr, rootWindow());
    xcb_windows.grab.map();
    xcb_windows.grab.raise();

    updateXTime();
    auto const cookie = xcb_grab_pointer_unchecked(
        connection(),
        false,
        xcb_windows.grab,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION
            | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
        XCB_GRAB_MODE_ASYNC,
        XCB_GRAB_MODE_ASYNC,
        xcb_windows.grab,
        Cursor::x11Cursor(control->move_resize().cursor),
        xTime());

    ScopedCPointer<xcb_grab_pointer_reply_t> pointerGrab(
        xcb_grab_pointer_reply(connection(), cookie, nullptr));
    if (!pointerGrab.isNull() && pointerGrab->status == XCB_GRAB_STATUS_SUCCESS) {
        has_grab = true;
    }

    if (!has_grab && grabXKeyboard(frameId()))
        has_grab = move_resize_has_keyboard_grab = true;
    if (!has_grab) {
        // at least one grab is necessary in order to be able to finish move/resize
        xcb_windows.grab.reset();
        return false;
    }

    return true;
}

void window::leaveMoveResize()
{
    if (needs_x_move) {
        // Do the deferred move
        xcb_windows.frame.move(geometries.buffer.topLeft());
        needs_x_move = false;
    }

    if (!win::is_resize(this)) {
        // tell the client about it's new final position
        send_synthetic_configure_notify(this);
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
    xcb_windows.grab.reset();

    if (sync_request.counter == XCB_NONE) {
        // don't forget to sanitize since the timeout will no more fire
        sync_request.isPending = false;
    }

    delete sync_request.timeout;
    sync_request.timeout = nullptr;
    Toplevel::leaveMoveResize();
}

bool window::isWaitingForMoveResizeSync() const
{
    return sync_request.isPending && win::is_resize(this);
}

void window::doResizeSync()
{
    if (!sync_request.timeout) {
        sync_request.timeout = new QTimer(this);
        connect(sync_request.timeout, &QTimer::timeout, this, [this] {
            win::perform_move_resize(this);
        });
        sync_request.timeout->setSingleShot(true);
    }

    if (sync_request.counter != XCB_NONE) {
        sync_request.timeout->start(250);
        send_sync_request(this);
    } else {
        // for clients not supporting the XSYNC protocol, we
        // limit the resizes to 30Hz to take pointless load from X11
        // and the client, the mouse is still moved at full speed
        // and no human can control faster resizes anyway
        sync_request.isPending = true;
        sync_request.timeout->start(33);
    }

    auto const move_resize_geo = control->move_resize().geometry;
    auto const moveResizeClientGeometry = win::frame_rect_to_client_rect(this, move_resize_geo);
    auto const moveResizeBufferGeometry = frame_rect_to_buffer_rect(this, move_resize_geo);

    // According to the Composite extension spec, a window will get a new pixmap allocated each time
    // it is mapped or resized. Given that we redirect frame windows and not client windows, we have
    // to resize the frame window in order to forcefully reallocate offscreen storage. If we don't
    // do this, then we might render partially updated client window. I know, it sucks.
    xcb_windows.frame.setGeometry(moveResizeBufferGeometry);
    xcb_windows.wrapper.setGeometry(
        QRect(win::to_client_pos(this, QPoint()), moveResizeClientGeometry.size()));
    xcb_windows.client.resize(moveResizeClientGeometry.size());
}

void window::doPerformMoveResize()
{
    if (sync_request.counter == XCB_NONE) {
        // client w/o XSYNC support. allow the next resize event
        // NEVER do this for clients with a valid counter
        // (leads to sync request races in some clients)
        sync_request.isPending = false;
    }

    reposition_geometry_tip();
}

}
