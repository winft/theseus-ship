/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "surface.h"
#include "xwl_control.h"

#include "base/x11/xcb/geometry_hints.h"
#include "base/x11/xcb/motif_hints.h"
#include "render/wayland/buffer.h"
#include "scene.h"
#include "toplevel.h"
#include "win/window_setup_base.h"
#include "win/x11/activation.h"
#include "win/x11/client.h"
#include "win/x11/damage.h"
#include "win/x11/deco.h"
#include "win/x11/desktop_space.h"
#include "win/x11/focus.h"
#include "win/x11/fullscreen.h"
#include "win/x11/geo.h"
#include "win/x11/maximize.h"
#include "win/x11/meta.h"
#include "win/x11/move.h"
#include "win/x11/scene.h"
#include "win/x11/shortcut.h"
#include "win/x11/sync.h"
#include "win/x11/transient.h"
#include "win/x11/types.h"
#include "win/x11/window.h"
#include "win/x11/window_release.h"
#include "win/x11/xcb_windows.h"

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Space>
class xwl_window : public Toplevel<Space>
{
public:
    using type = xwl_window<Space>;
    using control_t = xwl_control<xwl_window>;
    using render_t
        = render::window<typename Space::window_t, typename Space::base_t::render_t::compositor_t>;
    constexpr static bool is_toplevel{false};

    xwl_window(win::remnant remnant, Space& space)
        : Toplevel<Space>(std::move(remnant), space)
        , transient{std::make_unique<win::transient<type>>(this)}
        , motif_hints{space.atoms->motif_wm_hints}
    {
        this->space.windows_map.insert({this->meta.signal_id, this});
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
    }

    xwl_window(xcb_window_t xcb_win, Space& space)
        : Toplevel<Space>(space)
        , transient{std::make_unique<x11::transient<type>>(this)}
        , client_machine{new win::x11::client_machine}
        , motif_hints(space.atoms->motif_wm_hints)
    {
        this->space.windows_map.insert({this->meta.signal_id, this});
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
        window_setup_geometry(*this);

        this->xcb_windows.client.reset(xcb_win, false);
    }

    ~xwl_window()
    {
        x11::cleanup_window(*this);
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
        return x11::get_window_type(*this);
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
        return x11::get_wm_client_machine(*this, use_localhost);
    }

    xcb_window_t wmClientLeader() const
    {
        return x11::get_wm_client_leader(*this);
    }

    bool isLocalhost() const
    {
        assert(client_machine);
        return client_machine->is_local();
    }

    double opacity() const
    {
        return x11::get_opacity(*this);
    }

    void setOpacity(double new_opacity)
    {
        x11::set_opacity(*this, new_opacity);
    }

    xcb_window_t frameId() const
    {
        return x11::get_frame_id(*this);
    }

    QRegion render_region() const
    {
        return x11::get_render_region(*this);
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
        x11::show_context_help(*this);
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

    void finishCompositing()
    {
        x11::finish_compositing(*this);
    }

    void setBlockingCompositing(bool block)
    {
        x11::set_blocking_compositing(*this, block);
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
        x11::set_caption(this, this->meta.caption.normal, true);
    }

    bool isShown() const
    {
        return x11::is_shown(*this);
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

    // When another window is created, checks if this window is a child for it.
    void checkTransient(type* window)
    {
        x11::check_transient(*this, *window);
    }

    bool groupTransient() const
    {
        return static_cast<x11::transient<xwl_window>*>(this->transient.get())->lead_id
            == rootWindow();
    }

    type* findModal()
    {
        return x11::transient_find_modal(*this);
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
        return x11::user_can_set_fullscreen(*this);
    }

    void handle_update_fullscreen(bool full)
    {
        x11::propagate_fullscreen_update(this, full);
    }

    bool noBorder() const
    {
        return x11::deco_has_no_border(*this);
    }

    void setNoBorder(bool set)
    {
        x11::deco_set_no_border(*this, set);
    }

    void handle_update_no_border()
    {
        x11::check_set_no_border(this);
    }

    void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
    {
        x11::layout_decoration_rects(this, left, top, right, bottom);
    }

    void updateDecoration(bool check_workspace_pos, bool force = false)
    {
        x11::update_decoration(this, check_workspace_pos, force);
    }

    void handle_activated()
    {
        x11::update_user_time(this);
    }

    void takeFocus()
    {
        x11::focus_take(*this);
    }

    bool userCanSetNoBorder() const
    {
        return x11::deco_user_can_set_no_border(*this);
    }

    bool wantsInput() const
    {
        return x11::wants_input(*this);
    }

    void setShortcutInternal()
    {
        x11::shortcut_set_internal(*this);
    }

    bool hasStrut() const
    {
        return x11::has_strut(*this);
    }

    void showOnScreenEdge()
    {
        x11::show_on_screen_edge(*this);
    }

    void closeWindow()
    {
        x11::close_window(*this);
    }

    bool isCloseable() const
    {
        return x11::is_closeable(*this);
    }

    bool isMaximizable() const
    {
        return x11::geo_is_maximizable(*this);
    }

    bool isMinimizable() const
    {
        return x11::geo_is_minimizable(*this);
    }

    bool isMovable() const
    {
        return x11::is_movable(*this);
    }

    bool isMovableAcrossScreens() const
    {
        return x11::is_movable_across_screens(*this);
    }

    bool isResizable() const
    {
        return x11::is_resizable(*this);
    }

    void hideClient(bool hide)
    {
        x11::hide_window(*this, hide);
    }

    void update_maximized(maximize_mode mode)
    {
        x11::update_maximized(*this, mode);
    }

    bool doStartMoveResize()
    {
        return x11::do_start_move_resize(*this);
    }

    void leaveMoveResize()
    {
        x11::leave_move_resize(*this);
    }

    void doResizeSync()
    {
        x11::do_resize_sync(*this);
    }

    bool isWaitingForMoveResizeSync() const
    {
        return !pending_configures.empty();
    }

    bool belongsToSameApplication(type const* other, same_client_check checks) const
    {
        return x11::belong_to_same_application(this, other, checks);
    }

    bool belongsToDesktop() const
    {
        return x11::belongs_to_desktop(*this);
    }

    void doSetDesktop(int /*desktop*/, int /*was_desk*/)
    {
        x11::update_visibility(this);
    }

    bool isBlockingCompositing()
    {
        return blocks_compositing;
    }

    xcb_timestamp_t userTime() const
    {
        return x11::get_user_time(*this);
    }

    void doSetActive()
    {
        x11::do_set_active(*this);
    }

    void doMinimize()
    {
        x11::do_minimize(*this);
    }

    void setFrameGeometry(QRect const& rect)
    {
        x11::set_frame_geometry(*this, rect);
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
        x11::handle_kill_window(*this);
    }

    layer layer_for_dock() const
    {
        return win::layer_for_dock(*this);
    }

    void debug(QDebug& stream) const
    {
        x11::print_window_debug_info(*this, stream);
    }

    qreal bufferScale() const
    {
        return this->surface ? this->surface->state().scale : 1;
    }

    void handle_surface_damage(QRegion const& damage)
    {
        if (!this->render_data.ready_for_painting) {
            // avoid "setReadyForPainting()" function calling overhead
            if (this->sync_request.counter == XCB_NONE) {
                // cannot detect complete redraw, consider done now
                this->synced_geometry.init = false;
                set_ready_for_painting(*this);
            }
        }
        wayland::handle_surface_damage(*this, damage);
    }

    void add_scene_window_addon()
    {
        auto setup_buffer = [](auto& buffer) {
            using scene_t = typename Space::base_t::render_t::compositor_t::scene_t;
            using buffer_integration_t
                = render::wayland::buffer_win_integration<typename scene_t::buffer_t>;

            auto win_integrate = std::make_unique<buffer_integration_t>(buffer);
            auto update_helper = [&buffer]() {
                auto& win_integrate = static_cast<buffer_integration_t&>(*buffer.win_integration);
                std::visit(
                    overload{[&](auto&& win) { update_buffer(*win, win_integrate.external); }},
                    *buffer.window->ref_win);
            };
            win_integrate->update = update_helper;
            buffer.win_integration = std::move(win_integrate);
        };
        auto get_viewport = [](auto win, auto /*contentsRect*/) {
            // XWayland client's geometry must be taken from their content placement since the
            // buffer size is not in sync. So we only consider an explicitly set source rectangle.
            return std::visit(overload{[&](auto&& win) {
                                  return win->surface ? get_scaled_source_rectangle(*win)
                                                      : QRectF();
                              }},
                              win);
        };

        this->render->win_integration.setup_buffer = setup_buffer;
        this->render->win_integration.get_viewport = get_viewport;

        if (this->surface) {
            setup_scale_scene_notify(*this);
        }
    }

    bool has_pending_repaints() const
    {
        return !win::repaints(*this).isEmpty();
    }

    void setupCompositing()
    {
        assert(!this->remnant);
        assert(this->space.base.render->compositor->scene);
        assert(this->damage.handle == XCB_NONE);

        discard_shape(*this);
        this->render_data.damage_region = QRect({}, this->geo.size());

        add_scene_window(*this->space.base.render->compositor->scene, *this);

        if (this->control) {
            // for internalKeep()
            x11::update_visibility(this);
        }
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

    std::vector<x11::configure_event> pending_configures;

    // The geometry clients are configured with via the sync extension.
    x11::synced_geometry synced_geometry;

    QTimer* syncless_resize_retarder{nullptr};

    struct {
        QMetaObject::Connection edge_remove;
        QMetaObject::Connection edge_geometry;
    } connections;

    x11::mapping_state mapping{x11::mapping_state::withdrawn};

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
