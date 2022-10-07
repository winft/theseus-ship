/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "client.h"
#include "client_machine.h"
#include "control.h"
#include "damage.h"
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
#include "sync.h"
#include "transient.h"
#include "types.h"
#include "window_release.h"
#include "xcb.h"
#include "xcb_windows.h"

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
    using type = window<Space>;
    using control_t = x11::control<window>;
    using render_t
        = render::window<typename Space::window_t, typename Space::base_t::render_t::compositor_t>;
    constexpr static bool is_toplevel{false};

    window(win::remnant remnant, Space& space)
        : Toplevel<Space>(std::move(remnant), space)
        , transient{std::make_unique<win::transient<type>>(this)}
        , motif_hints{space.atoms->motif_wm_hints}
    {
        this->space.windows_map.insert({this->meta.signal_id, this});
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
    }

    window(xcb_window_t xcb_win, Space& space)
        : Toplevel<Space>(space)
        , transient{std::make_unique<x11::transient<type>>(this)}
        , client_machine{new win::x11::client_machine}
        , motif_hints(space.atoms->motif_wm_hints)
    {
        xcb_windows.client.reset(xcb_win, false);
        this->space.windows_map.insert({this->meta.signal_id, this});
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
        window_setup_geometry(*this);
    }

    ~window()
    {
        cleanup_window(*this);
    }

    bool isClient() const
    {
        return static_cast<bool>(this->control);
    }

    pid_t pid() const
    {
        return net_info->pid();
    }

    NET::WindowType get_window_type_direct() const
    {
        return x11::get_window_type_direct(*this);
    }

    NET::WindowType windowType() const
    {
        return get_window_type(*this);
    }

    QByteArray windowRole() const
    {
        if (this->remnant) {
            return this->remnant->data.window_role;
        }
        return net_info->windowRole();
    }

    x11::client_machine* get_client_machine() const
    {
        return client_machine;
    }

    QByteArray wmClientMachine(bool use_localhost) const
    {
        return get_wm_client_machine(*this, use_localhost);
    }

    bool isLocalhost() const
    {
        assert(client_machine);
        return client_machine->is_local();
    }

    double opacity() const
    {
        return get_opacity(*this);
    }

    void setOpacity(double new_opacity)
    {
        set_opacity(*this, new_opacity);
    }

    xcb_window_t frameId() const
    {
        return get_frame_id(*this);
    }

    QRegion render_region() const
    {
        return get_render_region(*this);
    }

    /**
     * Returns whether the window provides context help or not. If it does, you should show a help
     * menu item or a help button like '?' and call contextHelp() if this is invoked.
     */
    bool providesContextHelp() const
    {
        return net_info->supportsProtocol(NET::ContextHelpProtocol);
    }

    void showContextHelp()
    {
        show_context_help(*this);
    }

    void checkNoBorder()
    {
        setNoBorder(app_no_border);
    }

    bool wantsShadowToBeRendered() const
    {
        return wants_shadow_to_be_rendered(*this);
    }

    QSize resizeIncrements() const
    {
        return geometry_hints.resize_increments();
    }

    QRect iconGeometry() const
    {
        return x11::get_icon_geometry(*this);
    }

    void setupCompositing()
    {
        x11::setup_compositing(*this);
    }

    void finishCompositing()
    {
        x11::finish_compositing(*this);
    }

    void setBlockingCompositing(bool block)
    {
        x11::set_blocking_compositing(*this, block);
    }

    void add_scene_window_addon()
    {
        x11::add_scene_window_addon(*this);
    }

    bool has_pending_repaints() const
    {
        return !win::repaints(*this).isEmpty();
    }

    bool supportsWindowRules() const
    {
        return control != nullptr;
    }

    void applyWindowRules()
    {
        apply_window_rules(*this);
        setBlockingCompositing(net_info->isBlockingCompositing());
    }

    void updateWindowRules(rules::type selection)
    {
        if (!this->control) {
            // not fully setup yet
            return;
        }
        if (this->space.rule_book->areUpdatesDisabled()) {
            return;
        }
        rules::update_window(control->rules, *this, static_cast<int>(selection));
    }

    bool acceptsFocus() const
    {
        return net_info->input();
    }

    void updateCaption()
    {
        set_caption(this, this->meta.caption.normal, true);
    }

    bool isShown() const
    {
        return is_shown(*this);
    }

    bool isHiddenInternal() const
    {
        return hidden;
    }

    QSize minSize() const
    {
        return this->control->rules.checkMinSize(geometry_hints.min_size());
    }

    QSize maxSize() const
    {
        return this->control->rules.checkMaxSize(geometry_hints.max_size());
    }

    QSize basicUnit() const
    {
        return geometry_hints.resize_increments();
    }

    win::layer layer_for_dock() const
    {
        return win::layer_for_dock(*this);
    }

    // When another window is created, checks if this window is a child for it.
    void checkTransient(type* window)
    {
        check_transient(*this, *window);
    }

    bool groupTransient() const
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

    type* findModal()
    {
        return transient_find_modal(*this);
    }

    win::maximize_mode maximizeMode() const
    {
        return max_mode;
    }

    void setFullScreen(bool full, bool user = true)
    {
        win::update_fullscreen(this, full, user);
    }

    bool userCanSetFullScreen() const
    {
        return user_can_set_fullscreen(*this);
    }

    void handle_update_fullscreen(bool full)
    {
        propagate_fullscreen_update(this, full);
    }

    bool noBorder() const
    {
        return deco_has_no_border(*this);
    }

    void setNoBorder(bool set)
    {
        deco_set_no_border(*this, set);
    }

    void handle_update_no_border()
    {
        check_set_no_border(this);
    }

    void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
    {
        x11::layout_decoration_rects(this, left, top, right, bottom);
    }

    void updateDecoration(bool check_workspace_pos, bool force = false)
    {
        update_decoration(this, check_workspace_pos, force);
    }

    void handle_activated()
    {
        update_user_time(this);
    }

    void takeFocus()
    {
        focus_take(*this);
    }

    bool userCanSetNoBorder() const
    {
        return deco_user_can_set_no_border(*this);
    }

    bool wantsInput() const
    {
        return wants_input(*this);
    }

    void setShortcutInternal()
    {
        shortcut_set_internal(*this);
    }

    bool hasStrut() const
    {
        return has_strut(*this);
    }

    void showOnScreenEdge()
    {
        show_on_screen_edge(*this);
    }

    void closeWindow()
    {
        close_window(*this);
    }

    bool isCloseable() const
    {
        return is_closeable(*this);
    }

    bool isMaximizable() const
    {
        return geo_is_maximizable(*this);
    }

    bool isMinimizable() const
    {
        return geo_is_minimizable(*this);
    }

    bool isMovable() const
    {
        return is_movable(*this);
    }

    bool isMovableAcrossScreens() const
    {
        return is_movable_across_screens(*this);
    }

    bool isResizable() const
    {
        return is_resizable(*this);
    }

    void hideClient(bool hide)
    {
        hide_window(*this, hide);
    }

    void update_maximized(maximize_mode mode)
    {
        x11::update_maximized(*this, mode);
    }

    bool doStartMoveResize()
    {
        return do_start_move_resize(*this);
    }

    void leaveMoveResize()
    {
        x11::leave_move_resize(*this);
    }

    void doResizeSync()
    {
        do_resize_sync(*this);
    }

    bool isWaitingForMoveResizeSync() const
    {
        return !pending_configures.empty();
    }

    bool belongsToSameApplication(type const* other, win::same_client_check checks) const
    {
        return belong_to_same_application(this, other, checks);
    }

    bool belongsToDesktop() const
    {
        return belongs_to_desktop(*this);
    }

    void doSetDesktop(int /*desktop*/, int /*was_desk*/)
    {
        update_visibility(this);
    }

    bool isBlockingCompositing()
    {
        return blocks_compositing;
    }

    xcb_timestamp_t userTime() const
    {
        return get_user_time(*this);
    }

    void doSetActive()
    {
        do_set_active(*this);
    }

    void doMinimize()
    {
        do_minimize(*this);
    }

    void setFrameGeometry(QRect const& rect)
    {
        set_frame_geometry(*this, rect);
    }

    void apply_restore_geometry(QRect const& restore_geo)
    {
        setFrameGeometry(rectify_restore_geometry(this, restore_geo));
    }

    void restore_geometry_from_fullscreen()
    {
        x11::restore_geometry_from_fullscreen(*this);
    }

    void updateColorScheme()
    {
    }

    void killWindow()
    {
        handle_kill_window(*this);
    }

    void debug(QDebug& stream) const
    {
        print_window_debug_info(*this, stream);
    }

    std::unique_ptr<win::transient<type>> transient;
    std::unique_ptr<win::control<type>> control;
    std::unique_ptr<render_t> render;
    QString iconic_caption;

    NETWinInfo* net_info{nullptr};
    x11::xcb_windows xcb_windows;

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

    x11::sync_request sync_request;

    std::vector<configure_event> pending_configures;

    // The geometry clients are configured with via the sync extension.
    x11::synced_geometry synced_geometry;

    QTimer* syncless_resize_retarder{nullptr};

    struct {
        QMetaObject::Connection edge_remove;
        QMetaObject::Connection edge_geometry;
    } connections;

    mapping_state mapping{mapping_state::withdrawn};

    base::x11::xcb::geometry_hints geometry_hints;
    base::x11::xcb::motif_hints motif_hints;

    x11::damage damage;

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
