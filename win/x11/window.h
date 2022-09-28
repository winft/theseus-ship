/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "client.h"
#include "client_machine.h"
#include "control.h"
#include "deco.h"
#include "desktop_space.h"
#include "focus.h"
#include "fullscreen.h"
#include "geo.h"
#include "group.h"
#include "maximize.h"
#include "meta.h"
#include "move.h"
#include "scene.h"
#include "shortcut.h"
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
        return get_icon_geometry(*this);
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
        return is_shown(*this);
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
        return user_can_set_fullscreen(*this);
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
        focus_take(*this);
    }

    bool userCanSetNoBorder() const override
    {
        return deco_user_can_set_no_border(*this);
    }

    bool wantsInput() const override
    {
        return wants_input(*this);
    }

    bool performMouseCommand(base::options_qobject::MouseCommand command,
                             QPoint const& globalPos) override
    {
        return x11::perform_mouse_command(this, command, globalPos);
    }

    void setShortcutInternal() override
    {
        shortcut_set_internal(*this);
    }

    bool hasStrut() const override
    {
        return has_strut(*this);
    }

    void showOnScreenEdge() override
    {
        show_on_screen_edge(*this);
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
        return geo_is_maximizable(*this);
    }

    bool isMinimizable() const override
    {
        return geo_is_minimizable(*this);
    }

    bool isMovable() const override
    {
        return is_movable(*this);
    }

    bool isMovableAcrossScreens() const override
    {
        return is_movable_across_screens(*this);
    }

    bool isResizable() const override
    {
        return is_resizable(*this);
    }

    void hideClient(bool hide) override
    {
        hide_window(*this, hide);
    }

    void update_maximized(maximize_mode mode) override
    {
        x11::update_maximized(*this, mode);
    }

    bool doStartMoveResize() override
    {
        return do_start_move_resize(*this);
    }

    void leaveMoveResize() override
    {
        x11::leave_move_resize(*this);
    }

    void doResizeSync() override
    {
        do_resize_sync(*this);
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
        return belongs_to_desktop(*this);
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
        return get_user_time(*this);
    }

    void doSetActive() override
    {
        do_set_active(*this);
    }

    void doMinimize() override
    {
        do_minimize(*this);
    }

    void setFrameGeometry(QRect const& rect) override
    {
        set_frame_geometry(*this, rect);
    }

    void apply_restore_geometry(QRect const& restore_geo) override
    {
        setFrameGeometry(rectify_restore_geometry(this, restore_geo));
    }

    void restore_geometry_from_fullscreen() override
    {
        x11::restore_geometry_from_fullscreen(*this);
    }

    void updateColorScheme() override
    {
    }

    void killWindow() override
    {
        handle_kill_window(*this);
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
