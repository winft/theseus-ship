/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "client_machine.h"
#include "control.h"
#include "deco.h"
#include "fullscreen.h"
#include "geo.h"
#include "group.h"
#include "maximize.h"
#include "meta.h"
#include "scene.h"
#include "transient.h"
#include "types.h"
#include "window_release.h"
#include "xcb.h"

#include "base/x11/xcb/geometry_hints.h"
#include "base/x11/xcb/motif_hints.h"
#include "toplevel.h"
#include "utils/geo.h"
#include "win/fullscreen.h"
#include "win/meta.h"
#include "win/scene.h"
#include "win/window_setup_base.h"

#include <memory>
#include <vector>

namespace KWin::win::x11
{

template<typename Space>
class window : public Toplevel<Space>
{
public:
    using abstract_type = Toplevel<Space>;
    using control_t = x11::control<window>;
    constexpr static bool is_toplevel{false};

    window(win::remnant remnant, Space& space)
        : Toplevel<Space>(std::move(remnant), space)
        , motif_hints{space.atoms->motif_wm_hints}
    {
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
    }

    window(xcb_window_t xcb_win, Space& space)
        : Toplevel<Space>(new x11::transient<window>(this), space)
        , client_machine{new win::x11::client_machine}
        , motif_hints(space.atoms->motif_wm_hints)
    {
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
        window_setup_geometry(*this);

        this->xcb_window.reset(xcb_win, false);
    }

    ~window()
    {
        cleanup_window(*this);
    }

    bool isClient() const override
    {
        return static_cast<bool>(this->control);
    }

    NET::WindowType get_window_type_direct() const override
    {
        return x11::get_window_type_direct(*this);
    }

    NET::WindowType windowType() const override
    {
        return get_window_type(*this);
    }

    x11::client_machine* get_client_machine() const override
    {
        return client_machine;
    }

    QByteArray wmClientMachine(bool use_localhost) const override
    {
        return get_wm_client_machine(*this, use_localhost);
    }

    bool isLocalhost() const override
    {
        assert(client_machine);
        return client_machine->is_local();
    }

    double opacity() const override
    {
        return get_opacity(*this);
    }

    void setOpacity(double new_opacity) override
    {
        set_opacity(*this, new_opacity);
    }

    xcb_window_t frameId() const override
    {
        return get_frame_id(*this);
    }

    QRegion render_region() const override
    {
        return get_render_region(*this);
    }

    /**
     * Returns whether the window provides context help or not. If it does, you should show a help
     * menu item or a help button like '?' and call contextHelp() if this is invoked.
     */
    bool providesContextHelp() const override
    {
        return this->info->supportsProtocol(NET::ContextHelpProtocol);
    }

    void showContextHelp() override
    {
        show_context_help(*this);
    }

    void checkNoBorder() override
    {
        setNoBorder(app_no_border);
    }

    bool wantsShadowToBeRendered() const override
    {
        return wants_shadow_to_be_rendered(*this);
    }

    QSize resizeIncrements() const override
    {
        return geometry_hints.resize_increments();
    }

    QRect iconGeometry() const override
    {
        auto rect = this->info->iconGeometry();

        QRect geom(rect.pos.x, rect.pos.y, rect.size.width, rect.size.height);
        if (geom.isValid()) {
            return geom;
        }

        // Check all mainwindows of this window (recursively)
        for (auto mc : this->transient->leads()) {
            geom = mc->iconGeometry();
            if (geom.isValid()) {
                return geom;
            }
        }

        // No mainwindow (or their parents) with icon geometry was found
        return Toplevel<Space>::iconGeometry();
    }

    void setupCompositing() override
    {
        x11::setup_compositing(*this);
    }

    void finishCompositing() override
    {
        x11::finish_compositing(*this);
    }

    void setBlockingCompositing(bool block) override
    {
        x11::set_blocking_compositing(*this, block);
    }

    void add_scene_window_addon() override
    {
        x11::add_scene_window_addon(*this);
    }

    void applyWindowRules() override
    {
        apply_window_rules(*this);
        setBlockingCompositing(this->info->isBlockingCompositing());
    }

    void updateWindowRules(rules::type selection) override
    {
        if (!this->control) {
            // not fully setup yet
            return;
        }
        Toplevel<Space>::updateWindowRules(selection);
    }

    bool acceptsFocus() const override
    {
        return this->info->input();
    }

    void updateCaption() override
    {
        set_caption(this, this->meta.caption.normal, true);
    }

    bool isShown() const override
    {
        if (!this->control) {
            return true;
        }
        return !this->control->minimized && !hidden;
    }

    bool isHiddenInternal() const override
    {
        return hidden;
    }

    QSize minSize() const override
    {
        return this->control->rules.checkMinSize(geometry_hints.min_size());
    }

    QSize maxSize() const override
    {
        return this->control->rules.checkMaxSize(geometry_hints.max_size());
    }

    QSize basicUnit() const override
    {
        return geometry_hints.resize_increments();
    }

    // When another window is created, checks if this window is a child for it.
    void checkTransient(abstract_type* window) override
    {
        check_transient(*this, *window);
    }

    bool groupTransient() const override
    {
        // EWMH notes that a window with WM_TRANSIENT_FOR property sset to None should be treated
        // like a group transient [1], but internally we translate such setting early and only
        // identify a window as group transient when the transient-for/lead-id is set to the root
        // window.
        //
        // [1] https://specifications.freedesktop.org/wm-spec/wm-spec-latest.html#idm45623487728576
        //
        return static_cast<x11::transient<window>*>(this->transient.get())->lead_id == rootWindow();
    }

    abstract_type* findModal() override
    {
        return transient_find_modal(*this);
    }

    win::maximize_mode maximizeMode() const override
    {
        return max_mode;
    }

    void setFullScreen(bool full, bool user = true) override
    {
        win::update_fullscreen(this, full, user);
    }

    bool userCanSetFullScreen() const override
    {
        if (!this->control->can_fullscreen()) {
            return false;
        }
        return win::is_normal(this) || win::is_dialog(this);
    }

    void handle_update_fullscreen(bool full) override
    {
        propagate_fullscreen_update(this, full);
    }

    bool noBorder() const override
    {
        return deco_has_no_border(*this);
    }

    void setNoBorder(bool set) override
    {
        deco_set_no_border(*this, set);
    }

    void handle_update_no_border() override
    {
        check_set_no_border(this);
    }

    void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const override
    {
        x11::layout_decoration_rects(this, left, top, right, bottom);
    }

    void updateDecoration(bool check_workspace_pos, bool force = false) override
    {
        update_decoration(this, check_workspace_pos, force);
    }

    void handle_activated() override
    {
        update_user_time(this);
    }

    void takeFocus() override
    {
        if (this->control->rules.checkAcceptFocus(this->info->input())) {
            xcb_windows.client.focus();
        } else {
            // window cannot take input, at least withdraw urgency
            win::set_demands_attention(this, false);
        }

        if (this->info->supportsProtocol(NET::TakeFocusProtocol)) {
            kwinApp()->update_x11_time_from_clock();
            send_client_message(this->xcb_window,
                                this->space.atoms->wm_protocols,
                                this->space.atoms->wm_take_focus);
        }

        this->space.stacking.should_get_focus.push_back(this);

        // E.g. fullscreens have different layer when active/not-active.
        this->space.stacking.order.update_order();

        auto breakShowingDesktop = !this->control->keep_above;

        if (breakShowingDesktop) {
            for (auto const& c : group->members) {
                if (win::is_desktop(c)) {
                    breakShowingDesktop = false;
                    break;
                }
            }
        }

        if (breakShowingDesktop) {
            set_showing_desktop(this->space, false);
        }
    }

    bool userCanSetNoBorder() const override
    {
        return deco_user_can_set_no_border(*this);
    }

    bool wantsInput() const override
    {
        return this->control->rules.checkAcceptFocus(
            acceptsFocus() || this->info->supportsProtocol(NET::TakeFocusProtocol));
    }

    bool performMouseCommand(base::options_qobject::MouseCommand command,
                             QPoint const& globalPos) override
    {
        return x11::perform_mouse_command(this, command, globalPos);
    }

    void setShortcutInternal() override
    {
        updateCaption();
#if 0
        window_shortcut_updated(space, this);
#else
        // Workaround for kwin<->kglobalaccel deadlock, when KWin has X grab and the kded
        // kglobalaccel module tries to create the key grab. KWin should preferably grab
        // they keys itself anyway :(.
        QTimer::singleShot(
            0, this->qobject.get(), [this] { window_shortcut_updated(this->space, this); });
#endif
    }

    bool hasStrut() const override
    {
        NETExtendedStrut ext = strut(this);
        if (ext.left_width == 0 && ext.right_width == 0 && ext.top_width == 0
            && ext.bottom_width == 0) {
            return false;
        }
        return true;
    }

    void showOnScreenEdge() override
    {
        QObject::disconnect(connections.edge_remove);

        hideClient(false);
        win::set_keep_below(this, false);
        xcb_delete_property(
            connection(), this->xcb_window, this->space.atoms->kde_screen_edge_show);
    }

    void closeWindow() override
    {
        close_window(*this);
    }

    bool isCloseable() const override
    {
        return is_closeable(*this);
    }

    bool isMaximizable() const override
    {
        if (!isResizable() || win::is_toolbar(this) || win::is_applet_popup(this)) {
            // SELI isToolbar() ?
            return false;
        }
        if (this->control->rules.checkMaximize(win::maximize_mode::restore)
                == win::maximize_mode::restore
            && this->control->rules.checkMaximize(win::maximize_mode::full)
                != win::maximize_mode::restore) {
            return true;
        }
        return false;
    }

    bool isMinimizable() const override
    {
        if (win::is_special_window(this) && !this->transient->lead()) {
            return false;
        }
        if (win::is_applet_popup(this)) {
            return false;
        }
        if (!this->control->rules.checkMinimize(true)) {
            return false;
        }

        if (this->transient->lead()) {
            // #66868 - Let other xmms windows be minimized when the mainwindow is minimized
            auto shown_main_window = false;
            for (auto const& lead : this->transient->leads())
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

    bool isMovable() const override
    {
        if (!this->info->hasNETSupport() && !motif_hints.move()) {
            return false;
        }
        if (this->control->fullscreen) {
            return false;
        }
        if (win::is_special_window(this) && !win::is_splash(this) && !win::is_toolbar(this)) {
            // allow moving of splashscreens :)
            return false;
        }
        if (this->control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
            // forced position
            return false;
        }
        return true;
    }

    bool isMovableAcrossScreens() const override
    {
        if (!this->info->hasNETSupport() && !motif_hints.move()) {
            return false;
        }
        if (win::is_special_window(this) && !win::is_splash(this) && !win::is_toolbar(this)) {
            // allow moving of splashscreens :)
            return false;
        }
        if (this->control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
            // forced position
            return false;
        }
        return true;
    }

    bool isResizable() const override
    {
        if (!this->info->hasNETSupport() && !motif_hints.resize()) {
            return false;
        }
        if (this->geo.update.fullscreen) {
            return false;
        }
        if (win::is_special_window(this) || win::is_splash(this) || win::is_toolbar(this)) {
            return false;
        }
        if (this->control->rules.checkSize(QSize()).isValid()) {
            // forced size
            return false;
        }

        auto const mode = this->control->move_resize.contact;

        // TODO: we could just check with & on top and left.
        if ((mode == win::position::top || mode == win::position::top_left
             || mode == win::position::top_right || mode == win::position::left
             || mode == win::position::bottom_left)
            && this->control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
            return false;
        }

        auto min = minSize();
        auto max = maxSize();

        return min.width() < max.width() || min.height() < max.height();
    }

    void hideClient(bool hide) override
    {
        if (hidden == hide) {
            return;
        }
        hidden = hide;
        update_visibility(this);
    }

    void update_maximized(maximize_mode mode) override
    {
        if (!isResizable() || is_toolbar(this)) {
            return;
        }
        respect_maximizing_aspect(this, mode);
        win::update_maximized(this, mode);
    }

    bool doStartMoveResize() override
    {
        bool has_grab = false;

        // This reportedly improves smoothness of the moveresize operation,
        // something with Enter/LeaveNotify events, looks like XFree performance problem or
        // something *shrug* (https://lists.kde.org/?t=107302193400001&r=1&w=2)
        auto r = space_window_area(this->space, FullArea, this);

        xcb_windows.grab.create(r, XCB_WINDOW_CLASS_INPUT_ONLY, 0, nullptr, rootWindow());
        xcb_windows.grab.map();
        xcb_windows.grab.raise();

        kwinApp()->update_x11_time_from_clock();
        auto const cookie = xcb_grab_pointer_unchecked(
            connection(),
            false,
            xcb_windows.grab,
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
                | XCB_EVENT_MASK_LEAVE_WINDOW,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC,
            xcb_windows.grab,
            this->space.input->cursor->x11_cursor(this->control->move_resize.cursor),
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

    void leaveMoveResize() override
    {
        if (move_needs_server_update) {
            // Do the deferred move
            auto const frame_geo = this->geo.frame;
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

        leave_move_resize(*this);
    }

    void doResizeSync() override
    {
        auto const frame_geo = this->control->move_resize.geometry;

        if (sync_request.counter != XCB_NONE) {
            sync_geometry(this, frame_geo);
            update_server_geometry(this, frame_geo);
            return;
        }

        // Resizes without sync extension support need to be retarded to not flood clients with
        // geometry changes. Some clients can't handle this (for example Steam client).
        if (!syncless_resize_retarder) {
            syncless_resize_retarder = new QTimer(this->qobject.get());
            QObject::connect(
                syncless_resize_retarder, &QTimer::timeout, this->qobject.get(), [this] {
                    assert(!pending_configures.empty());
                    update_server_geometry(this, pending_configures.front().geometry.frame);
                    apply_pending_geometry(this, 0);
                });
            syncless_resize_retarder->setSingleShot(true);
        }

        if (pending_configures.empty()) {
            assert(!syncless_resize_retarder->isActive());
            pending_configures.push_back(
                {0, {frame_geo, QRect(), this->geo.update.max_mode, this->geo.update.fullscreen}});
            syncless_resize_retarder->start(16);
        } else {
            pending_configures.front().geometry.frame = frame_geo;
        }
    }

    bool isWaitingForMoveResizeSync() const override
    {
        return !pending_configures.empty();
    }

    bool belongsToSameApplication(abstract_type const* other,
                                  win::same_client_check checks) const override
    {
        auto c2 = dynamic_cast<const window*>(other);
        if (!c2) {
            return false;
        }
        return belong_to_same_application(this, c2, checks);
    }

    bool belongsToDesktop() const override
    {
        for (auto const& member : group->members) {
            if (win::is_desktop(member)) {
                return true;
            }
        }
        return false;
    }

    void doSetDesktop(int /*desktop*/, int /*was_desk*/) override
    {
        update_visibility(this);
    }

    bool isBlockingCompositing() override
    {
        return blocks_compositing;
    }

    xcb_timestamp_t userTime() const override
    {
        xcb_timestamp_t time = user_time;
        if (time == 0) {
            // Doesn't want focus after showing.
            return 0;
        }

        assert(group != nullptr);

        if (time == -1U
            || (group->user_time != -1U && NET::timestampCompare(group->user_time, time) > 0)) {
            time = group->user_time;
        }
        return time;
    }

    void doSetActive() override
    {
        // Demand attention again if it's still urgent.
        update_urgency(this);
        this->info->setState(this->control->active ? NET::Focused : NET::States(), NET::Focused);
    }

    void doMinimize() override
    {
        update_visibility(this);
        update_allowed_actions(this);
        propagate_minimized_to_transients(*this);
    }

    void setFrameGeometry(QRect const& rect) override
    {
        auto frame_geo = this->control->rules.checkGeometry(rect);

        this->geo.update.frame = frame_geo;

        if (this->geo.update.block) {
            this->geo.update.pending = win::pending_geometry::normal;
            return;
        }

        this->geo.update.pending = win::pending_geometry::none;

        auto const old_client_geo = synced_geometry.client;
        auto client_geo = frame_to_client_rect(this, frame_geo);

        if (!first_geo_synced) {
            // Initial sync-up after taking control of an unmapped window.

            if (sync_request.counter) {
                // The first sync can not be suppressed.
                assert(!sync_request.suppressed);
                sync_geometry(this, frame_geo);

                // Some Electron apps do not react to the first sync request and because of that
                // never show. It seems to be only a problem with apps based on Electron 9. This was
                // observed with Discord and balenaEtcher. For as long as there are common apps out
                // there still based on Electron 9 we use the following fallback timer to cancel the
                // wait after 1000 ms and instead set the window to directly show.
                auto fallback_timer = new QTimer(this->qobject.get());
                auto const serial = sync_request.update_request_number;
                QObject::connect(fallback_timer,
                                 &QTimer::timeout,
                                 this->qobject.get(),
                                 [this, fallback_timer, serial] {
                                     delete fallback_timer;

                                     if (pending_configures.empty()
                                         || pending_configures.front().update_request_number
                                             != serial) {
                                         return;
                                     }

                                     pending_configures.erase(pending_configures.begin());

                                     set_ready_for_painting(*this);
                                 });
                fallback_timer->setSingleShot(true);
                fallback_timer->start(1000);
            }

            update_server_geometry(this, frame_geo);
            send_synthetic_configure_notify(this, client_geo);
            do_set_geometry(frame_geo);
            do_set_fullscreen(this->geo.update.fullscreen);
            do_set_maximize_mode(this->geo.update.max_mode);
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
                    // The positional infomation in pending syncs must be updated to the new
                    // position.
                    event.geometry.frame.moveTo(frame_geo.topLeft());
                    event.geometry.client.moveTo(client_geo.topLeft());
                }
            }
        }

        update_server_geometry(this, frame_geo);

        do_set_geometry(frame_geo);
        do_set_fullscreen(this->geo.update.fullscreen);
        do_set_maximize_mode(this->geo.update.max_mode);

        // Always recalculate client geometry in case borders changed on fullscreen/maximize
        // changes.
        client_geo = frame_to_client_rect(this, frame_geo);

        // Always send a synthetic configure notify in the end to enforce updates to update
        // potential fullscreen/maximize changes. IntelliJ IDEA needed this to position its
        // unmanageds correctly.
        //
        // TODO(romangg): Restrain making this call to only being issued when really necessary.
        send_synthetic_configure_notify(this, client_geo);
    }

    void apply_restore_geometry(QRect const& restore_geo) override
    {
        setFrameGeometry(rectify_restore_geometry(this, restore_geo));
    }

    void restore_geometry_from_fullscreen() override
    {
        assert(!has_special_geometry_mode_besides_fullscreen(this));
        setFrameGeometry(rectify_fullscreen_restore_geometry(this));
        this->geo.restore.max = {};
    }

    void do_set_geometry(QRect const& frame_geo)
    {
        assert(this->control);

        auto const old_frame_geo = this->geo.frame;

        if (old_frame_geo == frame_geo && first_geo_synced) {
            return;
        }

        this->geo.frame = frame_geo;

        if (frame_to_render_rect(this, old_frame_geo).size()
            != frame_to_render_rect(this, frame_geo).size()) {
            discard_buffer(*this);
        }

        // TODO(romangg): Remove?
        win::set_current_output_by_window(this->space.base, *this);
        this->space.stacking.order.update_order();

        updateWindowRules(rules::type::position | rules::type::size);

        if (is_resize(this)) {
            perform_move_resize(this);
        }

        add_layer_repaint(*this, visible_rect(this, old_frame_geo));
        add_layer_repaint(*this, visible_rect(this, frame_geo));

        Q_EMIT this->qobject->frame_geometry_changed(old_frame_geo);

        // Must be done after signal is emitted so the screen margins are update.
        if (hasStrut()) {
            update_space_areas(this->space);
        }
    }

    void do_set_maximize_mode(win::maximize_mode mode)
    {
        if (mode == max_mode) {
            return;
        }

        auto old_mode = max_mode;
        max_mode = mode;

        update_allowed_actions(this);
        updateWindowRules(rules::type::maximize_horiz | rules::type::maximize_vert
                          | rules::type::position | rules::type::size);

        // Update decoration borders.
        if (auto deco = decoration(this); deco && deco->client()
            && !(kwinApp()->options->qobject->borderlessMaximizedWindows()
                 && mode == maximize_mode::full)) {
            auto const deco_client = decoration(this)->client().toStrongRef().data();

            if ((mode & maximize_mode::vertical) != (old_mode & maximize_mode::vertical)) {
                Q_EMIT deco_client->maximizedVerticallyChanged(
                    flags(mode & maximize_mode::vertical));
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
            this->control->deco.client->update_size();
        }

        // Need to update the server geometry in case the decoration changed.
        update_server_geometry(this, this->geo.update.frame);

        Q_EMIT this->qobject->maximize_mode_changed(mode);
    }

    void do_set_fullscreen(bool full)
    {
        full = this->control->rules.checkFullScreen(full);

        auto const old_full = this->control->fullscreen;
        if (old_full == full) {
            return;
        }

        if (old_full) {
            // May cause focus leave.
            // TODO: Must always be done when fullscreening to other output allowed.
            this->space.focusMousePos = this->space.input->cursor->pos();
        }

        this->control->fullscreen = full;

        if (full) {
            raise_window(&this->space, this);
        } else {
            // TODO(romangg): Can we do this also in setFullScreen? What about deco update?
            this->info->setState(full ? NET::FullScreen : NET::States(), NET::FullScreen);
            updateDecoration(false, false);

            // Need to update the server geometry in case the decoration changed.
            update_server_geometry(this, this->geo.update.frame);
        }

        // Active fullscreens gets a different layer.
        update_layer(this);
        updateWindowRules(rules::type::fullscreen | rules::type::position | rules::type::size);
        Q_EMIT this->qobject->fullScreenChanged();
    }

    void updateColorScheme() override
    {
    }

    void killWindow() override
    {
        handle_kill_window(*this);
    }

    void fetch_and_set_skip_close_animation()
    {
        set_skip_close_animation(*this, fetch_skip_close_animation(*this).to_bool());
    }

    void detectShape(xcb_window_t id)
    {
        const bool wasShape = this->is_shape;
        this->is_shape = base::x11::xcb::extensions::self()->has_shape(id);
        if (wasShape != this->is_shape) {
            Q_EMIT this->qobject->shapedChanged();
        }
    }

    void update_input_shape()
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
        if (!this->space.shape_helper_window.is_valid()) {
            this->space.shape_helper_window.create(QRect(0, 0, 1, 1));
        }

        this->space.shape_helper_window.resize(render_geometry(this).size());
        auto const deco_margin = QPoint(left_border(this), top_border(this));

        auto con = connection();

        xcb_shape_combine(con,
                          XCB_SHAPE_SO_SET,
                          XCB_SHAPE_SK_INPUT,
                          XCB_SHAPE_SK_BOUNDING,
                          this->space.shape_helper_window,
                          0,
                          0,
                          frameId());
        xcb_shape_combine(con,
                          XCB_SHAPE_SO_SUBTRACT,
                          XCB_SHAPE_SK_INPUT,
                          XCB_SHAPE_SK_BOUNDING,
                          this->space.shape_helper_window,
                          deco_margin.x(),
                          deco_margin.y(),
                          this->xcb_window);
        xcb_shape_combine(con,
                          XCB_SHAPE_SO_UNION,
                          XCB_SHAPE_SK_INPUT,
                          XCB_SHAPE_SK_INPUT,
                          this->space.shape_helper_window,
                          deco_margin.x(),
                          deco_margin.y(),
                          this->xcb_window);
        xcb_shape_combine(con,
                          XCB_SHAPE_SO_SET,
                          XCB_SHAPE_SK_INPUT,
                          XCB_SHAPE_SK_INPUT,
                          frameId(),
                          0,
                          0,
                          this->space.shape_helper_window);
    }

    /// Returns sessionId for this window, taken either from its window or from the leader.
    QByteArray sessionId() const
    {
        QByteArray result
            = base::x11::xcb::string_property(this->xcb_window, this->space.atoms->sm_client_id);
        if (result.isEmpty() && this->m_wmClientLeader
            && this->m_wmClientLeader != this->xcb_window) {
            result = base::x11::xcb::string_property(this->m_wmClientLeader,
                                                     this->space.atoms->sm_client_id);
        }
        return result;
    }

    /// Returns command property for this window, taken either from its window or from the leader.
    QByteArray wmCommand()
    {
        QByteArray result = base::x11::xcb::string_property(this->xcb_window, XCB_ATOM_WM_COMMAND);
        if (result.isEmpty() && this->m_wmClientLeader
            && this->m_wmClientLeader != this->xcb_window) {
            result = base::x11::xcb::string_property(this->m_wmClientLeader, XCB_ATOM_WM_COMMAND);
        }
        result.replace(0, ' ');
        return result;
    }

    // TODO(romangg): only required with Xwayland, move it to the child class.
    void clientMessageEvent(xcb_client_message_event_t* e)
    {
        if (e->type != this->space.atoms->wl_surface_id) {
            return;
        }

        this->surface_id = e->data.data32[0];
        Q_EMIT this->space.qobject->surface_id_changed(this->meta.signal_id, this->surface_id);
        Q_EMIT this->qobject->surfaceIdChanged(this->surface_id);
    }

    void debug(QDebug& stream) const override
    {
        print_window_debug_info(*this, stream);
    }

    QString iconic_caption;

    struct {
        // Most outer window that encompasses all other windows.
        base::x11::xcb::window outer{};

        // Window with the same dimensions as client.
        // TODO(romangg): Why do we need this again?
        base::x11::xcb::window wrapper{};

        // The actual client window.
        base::x11::xcb::window client{};

        // Including decoration.
        base::x11::xcb::window input{};

        // For move-resize operations.
        base::x11::xcb::window grab{};
    } xcb_windows;

    x11::client_machine* client_machine{nullptr};
    xcb_window_t m_wmClientLeader{XCB_WINDOW_NONE};

    bool blocks_compositing{false};
    uint deleting{0};
    bool has_scheduled_release{false};

    // True when X11 Server must be informed about the final location of a move on leaving the move.
    bool move_needs_server_update{false};
    bool move_resize_has_keyboard_grab{false};

    NET::WindowTypes supported_default_types{};
    NET::Actions allowed_actions{};

    uint user_no_border{0};
    uint app_no_border{0};

    win::maximize_mode max_mode{win::maximize_mode::restore};
    win::maximize_mode prev_max_mode{win::maximize_mode::restore};

    // Forcibly hidden by calling hide()
    uint hidden{0};

    xcb_timestamp_t ping_timestamp{XCB_TIME_CURRENT_TIME};
    xcb_timestamp_t user_time{XCB_TIME_CURRENT_TIME};

    qint64 kill_helper_pid{0};

    struct {
        xcb_sync_counter_t counter{XCB_NONE};
        xcb_sync_alarm_t alarm{XCB_NONE};

        // The update request number is the serial of our latest configure request.
        int64_t update_request_number{0};
        xcb_timestamp_t timestamp{XCB_NONE};

        int suppressed{0};
    } sync_request;

    struct configure_event {
        int64_t update_request_number{0};

        // Geometry to apply after a resize operation has been completed.
        struct {
            QRect frame;
            // TODO(romangg): instead of client geometry remember deco and extents margins?
            QRect client;
            maximize_mode max_mode{maximize_mode::restore};
            bool fullscreen{false};
        } geometry;
    };
    std::vector<configure_event> pending_configures;

    // The geometry clients are configured with via the sync extension.
    struct {
        QRect frame;
        QRect client;
        maximize_mode max_mode{maximize_mode::restore};
        bool fullscreen{false};
    } synced_geometry;

    bool first_geo_synced{false};

    QTimer* syncless_resize_retarder{nullptr};

    struct {
        QMetaObject::Connection edge_remove;
        QMetaObject::Connection edge_geometry;
    } connections;

    mapping_state mapping{mapping_state::withdrawn};

    base::x11::xcb::geometry_hints geometry_hints;
    base::x11::xcb::motif_hints motif_hints;

    xcb_damage_damage_t damage_handle{XCB_NONE};
    bool is_damage_reply_pending{false};
    xcb_xfixes_fetch_region_cookie_t damage_region_cookie;

    QTimer* focus_out_timer{nullptr};
    QTimer* ping_timer{nullptr};

    QPoint input_offset;
    mutable QRegion render_shape;

    int sm_stacking_order{-1};

    x11::group<Space>* group{nullptr};

    xcb_visualid_t xcb_visual{XCB_NONE};
    xcb_colormap_t colormap{XCB_COLORMAP_NONE};

    // Only used as a cache for window as a remnant.
    NET::WindowType window_type{NET::Normal};
};

}
