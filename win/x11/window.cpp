/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "client.h"
#include "deco.h"
#include "fullscreen.h"
#include "geo.h"
#include "hide.h"
#include "maximize.h"
#include "meta.h"
#include "transient.h"
#include "unmanaged.h"
#include "window_release.h"

#include "base/x11/grabs.h"
#include "render/x11/buffer.h"
#include "render/x11/shadow.h"
#include "rules/rules.h"
#include "win/deco.h"
#include "win/deco/window.h"
#include "win/layers.h"
#include "win/remnant.h"
#include "win/stacking.h"
#include "win/stacking_order.h"

#include <KDecoration2/DecoratedClient>

namespace KWin::win::x11
{

window::window(win::remnant remnant, win::space& space)
    : Toplevel(std::move(remnant), space)
    , motif_hints{space.atoms->motif_wm_hints}
{
}

window::window(xcb_window_t xcb_win, win::space& space)
    : Toplevel(new x11::transient(this), space)
    , motif_hints(space.atoms->motif_wm_hints)
{
    xcb_window.reset(xcb_win, false);
    client_machine = new win::x11::client_machine;
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
    assert(xcb_windows.outer == XCB_WINDOW_NONE);
}

bool window::isClient() const
{
    return static_cast<bool>(control);
}

xcb_window_t window::frameId() const
{
    if (remnant) {
        return remnant->data.frame;
    }
    if (!control) {
        return Toplevel::frameId();
    }
    return xcb_windows.outer;
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
        send_client_message(
            xcb_window, space.atoms->wm_protocols, space.atoms->net_wm_context_help);
    }
}

bool window::noBorder() const
{
    if (remnant) {
        return remnant->data.no_border;
    }
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

    if (decoration(this)) {
        control->deco().client->update_size();
    }
}

bool window::userCanSetNoBorder() const
{
    // CSD in general allow no change by user, also not possible when fullscreen.
    return client_frame_extents.isNull() && !control->fullscreen();
}

void window::checkNoBorder()
{
    setNoBorder(app_no_border);
}

bool window::wantsShadowToBeRendered() const
{
    return control && !control->fullscreen() && maximizeMode() != win::maximize_mode::full;
}

QSize window::resizeIncrements() const
{
    return geometry_hints.resize_increments();
}

static base::x11::xcb::window shape_helper_window(XCB_WINDOW_NONE);

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

    if (!base::x11::xcb::extensions::self()->is_shape_input_available()) {
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
    if (!shape_helper_window.is_valid()) {
        shape_helper_window.create(QRect(0, 0, 1, 1));
    }

    shape_helper_window.resize(render_geometry(this).size());
    auto const deco_margin = QPoint(left_border(this), top_border(this));

    auto con = connection();

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
                      deco_margin.x(),
                      deco_margin.y(),
                      xcb_window);
    xcb_shape_combine(con,
                      XCB_SHAPE_SO_UNION,
                      XCB_SHAPE_SK_INPUT,
                      XCB_SHAPE_SK_INPUT,
                      shape_helper_window,
                      deco_margin.x(),
                      deco_margin.y(),
                      xcb_window);
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

bool window::setupCompositing()
{
    return x11::setup_compositing(*this);
}

void window::finishCompositing()
{
    Toplevel::finishCompositing();
    destroy_damage_handle(*this);

    // For safety in case KWin is just resizing the window.
    // TODO(romangg): Is this really needed?
    reset_have_resize_effect(*this);
}

void window::setBlockingCompositing(bool block)
{
    auto const usedToBlock = blocks_compositing;
    blocks_compositing = control->rules().checkBlockCompositing(
        block && kwinApp()->options->windowsBlockCompositing());

    if (usedToBlock != blocks_compositing) {
        Q_EMIT blockingCompositingChanged(blocks_compositing ? this : nullptr);
    }
}

void window::add_scene_window_addon()
{
    auto& atoms = space.atoms;
    render->shadow_windowing.create = [&](auto&& win) {
        return render::x11::create_shadow<render::shadow, Toplevel>(win, atoms->kde_net_wm_shadow);
    };
    render->shadow_windowing.update = [&](auto&& shadow) {
        return render::x11::read_and_update_shadow<render::shadow>(shadow,
                                                                   atoms->kde_net_wm_shadow);
    };

    auto setup_buffer = [this](auto& buffer) {
        auto win_integrate = std::make_unique<render::x11::buffer_win_integration>(buffer);
        auto update_helper = [&buffer]() {
            auto& win_integrate
                = static_cast<render::x11::buffer_win_integration&>(*buffer.win_integration);
            create_window_buffer(buffer.toplevel(), win_integrate);
        };
        win_integrate->update = update_helper;
        buffer.win_integration = std::move(win_integrate);
    };
    render->win_integration.setup_buffer = setup_buffer;
}

void window::damageNotifyEvent()
{
    if (!control) {
        Toplevel::damageNotifyEvent();
        return;
    }

    if (isWaitingForMoveResizeSync()) {
        m_isDamaged = true;
        return;
    }

    if (!ready_for_painting) {
        // avoid "setReadyForPainting()" function calling overhead
        if (sync_request.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            setReadyForPainting();
        }
    }

    Toplevel::damageNotifyEvent();
}

void window::applyWindowRules()
{
    Toplevel::applyWindowRules();
    setBlockingCompositing(info->isBlockingCompositing());
}

void window::updateWindowRules(Rules::Types selection)
{
    if (!control) {
        // not fully setup yet
        return;
    }
    Toplevel::updateWindowRules(selection);
}

void window::closeWindow()
{
    if (!isCloseable()) {
        return;
    }

    // Update user time, because the window may create a confirming dialog.
    update_user_time(this);

    if (info->supportsProtocol(NET::DeleteWindowProtocol)) {
        send_client_message(xcb_window, space.atoms->wm_protocols, space.atoms->wm_delete_window);
        ping(this);
    } else {
        // Client will not react on wm_delete_window. We have not choice
        // but destroy his connection to the XServer.
        killWindow();
    }
}

QSize window::minSize() const
{
    return control->rules().checkMinSize(geometry_hints.min_size());
}

QSize window::maxSize() const
{
    return control->rules().checkMaxSize(geometry_hints.max_size());
}

QSize window::basicUnit() const
{
    return geometry_hints.resize_increments();
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
    if (win::is_special_window(this) && !transient()->lead()) {
        return false;
    }
    if (!control->rules().checkMinimize(true)) {
        return false;
    }

    if (transient()->lead()) {
        // #66868 - Let other xmms windows be minimized when the mainwindow is minimized
        auto shown_main_window = false;
        for (auto const& lead : transient()->leads())
            if (lead->isShown()) {
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
    if (control->rules().checkPosition(geo::invalid_point) != geo::invalid_point) {
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
    if (control->rules().checkPosition(geo::invalid_point) != geo::invalid_point) {
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
    if (geometry_update.fullscreen) {
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
        && control->rules().checkPosition(geo::invalid_point) != geo::invalid_point) {
        return false;
    }

    auto min = minSize();
    auto max = maxSize();

    return min.width() < max.width() || min.height() < max.height();
}

bool window::groupTransient() const
{
    // EWMH notes that a window with WM_TRANSIENT_FOR property sset to None should be treated like
    // a group transient [1], but internally we translate such setting early and only identify a
    // window as group transient when the transient-for/lead-id is set to the root window.
    //
    // [1] https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html#idm45623487728576
    //
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
        kwinApp()->update_x11_time_from_clock();
        send_client_message(xcb_window, space.atoms->wm_protocols, space.atoms->wm_take_focus);
    }

    space.setShouldGetFocus(this);
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
        space.setShowingDesktop(false);
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

bool window::isShown() const
{
    if (!control) {
        return true;
    }
    return !control->minimized() && !hidden;
}

bool window::isHiddenInternal() const
{
    return hidden;
}

bool window::performMouseCommand(base::options::MouseCommand command, QPoint const& globalPos)
{
    return x11::perform_mouse_command(this, command, globalPos);
}

void window::setShortcutInternal()
{
    updateCaption();
#if 0
    space.clientShortcutUpdated(this);
#else
    // Workaround for kwin<->kglobalaccel deadlock, when KWin has X grab and the kded
    // kglobalaccel module tries to create the key grab. KWin should preferably grab
    // they keys itself anyway :(.
    QTimer::singleShot(0, this, std::bind(&space::clientShortcutUpdated, &space, this));
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

void window::addDamage(QRegion const& damage)
{
    if (!ready_for_painting) {
        // avoid "setReadyForPainting()" function calling overhead
        if (sync_request.counter == XCB_NONE) {
            // cannot detect complete redraw, consider done now
            first_geo_synced = true;
            setReadyForPainting();
        }
    }
    Toplevel::addDamage(damage);
}

maximize_mode window::maximizeMode() const
{
    return max_mode;
}

void window::setFrameGeometry(QRect const& rect)
{
    auto frame_geo = control->rules().checkGeometry(rect);

    geometry_update.frame = frame_geo;

    if (geometry_update.block) {
        geometry_update.pending = win::pending_geometry::normal;
        return;
    }

    geometry_update.pending = win::pending_geometry::none;

    auto const old_client_geo = synced_geometry.client;
    auto client_geo = frame_to_client_rect(this, frame_geo);

    if (!first_geo_synced) {
        // Initial sync-up after taking control of an unmapped window.

        if (sync_request.counter) {
            // The first sync can not be suppressed.
            assert(!sync_request.suppressed);
            sync_geometry(this, frame_geo);

            // Some Electron apps do not react to the first sync request and because of that never
            // show. It seems to be only a problem with apps based on Electron 9. This was observed
            // with Discord and balenaEtcher.
            // For as long as there are common apps out there still based on Electron 9 we use the
            // following fallback timer to cancel the wait after 1000 ms and instead set the window
            // to directly show.
            auto fallback_timer = new QTimer(this);
            auto const serial = sync_request.update_request_number;
            connect(fallback_timer, &QTimer::timeout, this, [this, fallback_timer, serial] {
                delete fallback_timer;

                if (pending_configures.empty()
                    || pending_configures.front().update_request_number != serial) {
                    return;
                }

                pending_configures.erase(pending_configures.begin());

                setReadyForPainting();
            });
            fallback_timer->setSingleShot(true);
            fallback_timer->start(1000);
        }

        update_server_geometry(this, frame_geo);
        send_synthetic_configure_notify(this, client_geo);
        do_set_geometry(frame_geo);
        do_set_fullscreen(geometry_update.fullscreen);
        do_set_maximize_mode(geometry_update.max_mode);
        first_geo_synced = true;
        return;
    }

    if (sync_request.counter) {
        if (sync_request.suppressed) {
            // Adapt previous syncs so we don't update to an old geometry when client returns.
            for (auto& configure : pending_configures) {
                configure.geometry.client = client_geo;
                configure.geometry.frame = frame_geo;
            }
        } else {
            if (old_client_geo.size() != client_geo.size()) {
                // Size changed. Request a new one from the client and wait on it.
                sync_geometry(this, frame_geo);
                update_server_geometry(this, frame_geo);
                return;
            }

            // Move without size change.
            for (auto& event : pending_configures) {
                // The positional infomation in pending syncs must be updated to the new position.
                event.geometry.frame.moveTo(frame_geo.topLeft());
                event.geometry.client.moveTo(client_geo.topLeft());
            }
        }
    }

    update_server_geometry(this, frame_geo);

    do_set_geometry(frame_geo);
    do_set_fullscreen(geometry_update.fullscreen);
    do_set_maximize_mode(geometry_update.max_mode);

    // Always recalculate client geometry in case borders changed on fullscreen/maximize changes.
    client_geo = frame_to_client_rect(this, frame_geo);

    // Always send a synthetic configure notify in the end to enforce updates to update potential
    // fullscreen/maximize changes. IntelliJ IDEA needed this to position its unmanageds correctly.
    //
    // TODO(romangg): Restrain making this call to only being issued when really necessary.
    send_synthetic_configure_notify(this, client_geo);
}

void window::do_set_geometry(QRect const& frame_geo)
{
    assert(control);

    auto const old_frame_geo = frameGeometry();

    if (old_frame_geo == frame_geo && first_geo_synced) {
        return;
    }

    set_frame_geometry(frame_geo);

    if (frame_to_render_rect(this, old_frame_geo).size()
        != frame_to_render_rect(this, frame_geo).size()) {
        discard_buffer();
    }

    // TODO(romangg): Remove?
    win::set_current_output_by_window(kwinApp()->get_base(), *this);
    space.stacking_order->update_order();

    updateWindowRules(static_cast<Rules::Types>(Rules::Position | Rules::Size));

    if (is_resize(this)) {
        perform_move_resize(this);
    }

    addLayerRepaint(visible_rect(this, old_frame_geo));
    addLayerRepaint(visible_rect(this, frame_geo));

    Q_EMIT frame_geometry_changed(this, old_frame_geo);

    // Must be done after signal is emitted so the screen margins are update.
    if (hasStrut()) {
        space.updateClientArea();
    }
}

void window::do_set_maximize_mode(maximize_mode mode)
{
    if (mode == max_mode) {
        return;
    }

    auto old_mode = max_mode;
    max_mode = mode;

    update_allowed_actions(this);
    updateWindowRules(static_cast<Rules::Types>(Rules::MaximizeHoriz | Rules::MaximizeVert
                                                | Rules::Position | Rules::Size));

    // Update decoration borders.
    if (auto deco = decoration(this); deco && deco->client()
        && !(kwinApp()->options->borderlessMaximizedWindows() && mode == maximize_mode::full)) {
        auto const deco_client = decoration(this)->client().toStrongRef().data();

        if ((mode & maximize_mode::vertical) != (old_mode & maximize_mode::vertical)) {
            Q_EMIT deco_client->maximizedVerticallyChanged(flags(mode & maximize_mode::vertical));
        }
        if ((mode & maximize_mode::horizontal) != (old_mode & maximize_mode::horizontal)) {
            Q_EMIT deco_client->maximizedHorizontallyChanged(
                flags(mode & maximize_mode::horizontal));
        }
        if ((mode == maximize_mode::full) != (old_mode == maximize_mode::full)) {
            Q_EMIT deco_client->maximizedChanged(flags(mode & maximize_mode::full));
        }
    }

    // TODO(romangg): Can we do this also in update_maximized? What about deco update?
    if (decoration(this)) {
        control->deco().client->update_size();
    }

    // Need to update the server geometry in case the decoration changed.
    update_server_geometry(this, geometry_update.frame);

    Q_EMIT clientMaximizedStateChanged(this, mode);
    Q_EMIT clientMaximizedStateChanged(this,
                                       flags(mode & win::maximize_mode::horizontal),
                                       flags(mode & win::maximize_mode::vertical));
}

void window::do_set_fullscreen(bool full)
{
    full = control->rules().checkFullScreen(full);

    auto const old_full = control->fullscreen();
    if (old_full == full) {
        return;
    }

    if (old_full) {
        // May cause focus leave.
        // TODO: Must always be done when fullscreening to other output allowed.
        space.focusMousePos = input::get_cursor()->pos();
    }

    control->set_fullscreen(full);

    if (full) {
        raise_window(&space, this);
    } else {
        // TODO(romangg): Can we do this also in setFullScreen? What about deco update?
        info->setState(full ? NET::FullScreen : NET::States(), NET::FullScreen);
        updateDecoration(false, false);

        // Need to update the server geometry in case the decoration changed.
        update_server_geometry(this, geometry_update.frame);
    }

    // Active fullscreens gets a different layer.
    update_layer(this);

    updateWindowRules(static_cast<Rules::Types>(Rules::Fullscreen | Rules::Position | Rules::Size));

    // TODO(romangg): Is it really important for scripts if the fullscreen was triggered by the app
    //                or the user? For now just pretend that it was always the user.
    Q_EMIT client_fullscreen_set(this, full, true);
    Q_EMIT fullScreenChanged();
}

void window::update_maximized(maximize_mode mode)
{
    win::update_maximized(this, mode);
}

void window::setFullScreen(bool full, bool user)
{
    win::update_fullscreen(this, full, user);
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

group const* window::group() const
{
    return in_group;
}

group* window::group()
{
    return in_group;
}

void window::checkTransient(Toplevel* window)
{
    auto id = static_cast<xcb_window_t>(window->xcb_window);
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
    if (remnant) {
        return remnant->data.layout_decoration_rects(left, top, right, bottom);
    }
    layout_decoration_rects(this, left, top, right, bottom);
}

void window::updateDecoration(bool check_workspace_pos, bool force)
{
    update_decoration(this, check_workspace_pos, force);
}

void window::updateColorScheme()
{
}

bool window::isBlockingCompositing()
{
    return blocks_compositing;
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

/**
 * Kills the window via XKill
 */
void window::killWindow()
{
    qCDebug(KWIN_CORE) << "window::killWindow():" << win::caption(this);
    kill_process(this, false);

    // Always kill this client at the server
    xcb_windows.client.kill();

    destroy_window(this);
}

void window::getResourceClass()
{
    setResourceClass(QByteArray(info->windowClassName()).toLower(),
                     QByteArray(info->windowClassClass()).toLower());
}

void window::getWmClientMachine()
{
    client_machine->resolve(xcb_window, wmClientLeader());
}

base::x11::xcb::property window::fetchWmClientLeader() const
{
    return base::x11::xcb::property(
        false, xcb_window, space.atoms->wm_client_leader, XCB_ATOM_WINDOW, 0, 10000);
}

void window::readWmClientLeader(base::x11::xcb::property& prop)
{
    m_wmClientLeader = prop.value<xcb_window_t>(xcb_window);
}

void window::getWmClientLeader()
{
    auto prop = fetchWmClientLeader();
    readWmClientLeader(prop);
}

void window::getWmOpaqueRegion()
{
    const auto rects = info->opaqueRegion();
    QRegion new_opaque_region;
    for (const auto& r : rects) {
        new_opaque_region += QRect(r.pos.x, r.pos.y, r.size.width, r.size.height);
    }

    opaque_region = new_opaque_region;
}

void window::getSkipCloseAnimation()
{
    setSkipCloseAnimation(fetch_skip_close_animation(*this).to_bool());
}

void window::detectShape(xcb_window_t id)
{
    const bool wasShape = is_shape;
    is_shape = base::x11::xcb::extensions::self()->has_shape(id);
    if (wasShape != is_shape) {
        Q_EMIT shapedChanged();
    }
}

/**
 * Returns sessionId for this client,
 * taken either from its window or from the leader window.
 */
QByteArray window::sessionId() const
{
    QByteArray result = base::x11::xcb::string_property(xcb_window, space.atoms->sm_client_id);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != xcb_window) {
        result = base::x11::xcb::string_property(m_wmClientLeader, space.atoms->sm_client_id);
    }
    return result;
}

/**
 * Returns command property for this client,
 * taken either from its window or from the leader window.
 */
QByteArray window::wmCommand()
{
    QByteArray result = base::x11::xcb::string_property(xcb_window, XCB_ATOM_WM_COMMAND);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != xcb_window) {
        result = base::x11::xcb::string_property(m_wmClientLeader, XCB_ATOM_WM_COMMAND);
    }
    result.replace(0, ' ');
    return result;
}

void window::clientMessageEvent(xcb_client_message_event_t* e)
{
    if (e->type != space.atoms->wl_surface_id) {
        return;
    }

    surface_id = e->data.data32[0];
    Q_EMIT space.qobject->surface_id_changed(this, surface_id);
    Q_EMIT surfaceIdChanged(surface_id);
}

bool window::resourceMatch(window const* c1, window const* c2)
{
    return c1->resource_class == c2->resource_class;
}

void window::debug(QDebug& stream) const
{
    if (remnant) {
        stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
        return;
    }

    std::string type = "unmanaged";
    std::string caption = "";
    if (control) {
        type = "managed";
        caption = win::caption(this).toStdString();
    }

    stream.nospace();
    stream << "\'x11::window"
           << "(" << QString::fromStdString(type) << "):" << xcb_window << ";"
           << ";WMCLASS:" << resource_class << ":" << resource_name
           << ";Caption:" << QString::fromStdString(caption) << "\'";
}

void window::doMinimize()
{
    update_visibility(this);
    update_allowed_actions(this);
    space.updateMinimizedOfTransients(this);
}

void window::showOnScreenEdge()
{
    disconnect(connections.edge_remove);

    hideClient(false);
    win::set_keep_below(this, false);
    xcb_delete_property(connection(), xcb_window, space.atoms->kde_screen_edge_show);
}

bool window::doStartMoveResize()
{
    bool has_grab = false;

    // This reportedly improves smoothness of the moveresize operation,
    // something with Enter/LeaveNotify events, looks like XFree performance problem or something
    // *shrug* (https://lists.kde.org/?t=107302193400001&r=1&w=2)
    auto r = space.clientArea(FullArea, this);

    xcb_windows.grab.create(r, XCB_WINDOW_CLASS_INPUT_ONLY, 0, nullptr, rootWindow());
    xcb_windows.grab.map();
    xcb_windows.grab.raise();

    kwinApp()->update_x11_time_from_clock();
    auto const cookie = xcb_grab_pointer_unchecked(
        connection(),
        false,
        xcb_windows.grab,
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION
            | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
        XCB_GRAB_MODE_ASYNC,
        XCB_GRAB_MODE_ASYNC,
        xcb_windows.grab,
        input::get_cursor()->x11_cursor(control->move_resize().cursor),
        xTime());

    unique_cptr<xcb_grab_pointer_reply_t> pointerGrab(
        xcb_grab_pointer_reply(connection(), cookie, nullptr));
    if (pointerGrab && pointerGrab->status == XCB_GRAB_STATUS_SUCCESS) {
        has_grab = true;
    }

    if (!has_grab && base::x11::grab_keyboard(frameId()))
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
    if (move_needs_server_update) {
        // Do the deferred move
        auto const frame_geo = frameGeometry();
        auto const client_geo = frame_to_client_rect(this, frame_geo);
        auto const outer_pos = frame_to_render_rect(this, frame_geo).topLeft();

        xcb_windows.outer.move(outer_pos);
        send_synthetic_configure_notify(this, client_geo);

        synced_geometry.frame = frame_geo;
        synced_geometry.client = client_geo;

        move_needs_server_update = false;
    }

    if (move_resize_has_keyboard_grab) {
        base::x11::ungrab_keyboard();
    }

    move_resize_has_keyboard_grab = false;
    xcb_ungrab_pointer(connection(), xTime());
    xcb_windows.grab.reset();

    Toplevel::leaveMoveResize();
}

bool window::isWaitingForMoveResizeSync() const
{
    return !pending_configures.empty();
}

void window::doResizeSync()
{
    auto const frame_geo = control->move_resize().geometry;

    if (sync_request.counter != XCB_NONE) {
        sync_geometry(this, frame_geo);
        update_server_geometry(this, frame_geo);
        return;
    }

    // Resizes without sync extension support need to be retarded to not flood clients with
    // geometry changes. Some clients can't handle this (for example Steam client).
    if (!syncless_resize_retarder) {
        syncless_resize_retarder = new QTimer(this);
        connect(syncless_resize_retarder, &QTimer::timeout, this, [this] {
            assert(!pending_configures.empty());
            update_server_geometry(this, pending_configures.front().geometry.frame);
            apply_pending_geometry(this, 0);
        });
        syncless_resize_retarder->setSingleShot(true);
    }

    if (pending_configures.empty()) {
        assert(!syncless_resize_retarder->isActive());
        pending_configures.push_back(
            {0, {frame_geo, QRect(), geometry_update.max_mode, geometry_update.fullscreen}});
        syncless_resize_retarder->start(16);
    } else {
        pending_configures.front().geometry.frame = frame_geo;
    }
}

}
