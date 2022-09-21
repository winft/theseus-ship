/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "scene.h"
#include "subsurface.h"
#include "surface.h"
#include "xdg_shell.h"
#include "xdg_shell_control.h"

#include "render/platform.h"
#include "render/wayland/buffer.h"
#include "render/wayland/shadow.h"
#include "toplevel.h"
#include "utils/geo.h"
#include "win/fullscreen.h"
#include "win/geo_block.h"
#include "win/geo_restrict.h"
#include "win/maximize.h"
#include "win/placement.h"
#include "win/rules/find.h"
#include "win/scene.h"
#include "win/window_setup_base.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/input_method_v2.h>
#include <Wrapland/Server/layer_shell_v1.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/xdg_decoration.h>
#include <Wrapland/Server/xdg_shell.h>

#include <csignal>
#include <memory>
#include <unistd.h>
#include <vector>

namespace KWin::win::wayland
{

template<typename Space>
class window : public Toplevel<Space>
{
public:
    using abstract_type = Toplevel<Space>;
    using xdg_shell_control_t = xdg_shell_control<window<Space>>;
    using layer_control_t = wayland::control<window<Space>>;
    constexpr static bool is_toplevel{false};

    enum class ping_reason {
        close = 0,
        focus,
    };

    window(win::remnant remnant, Space& space)
        : Toplevel<Space>(std::move(remnant), space)
    {
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
    }

    window(Wrapland::Server::Surface* surface, Space& space)
        : Toplevel<Space>(space)
    {
        Toplevel<Space>::qobject = std::make_unique<window_qobject>();
        window_setup_geometry(*this);

        QObject::connect(surface,
                         &Wrapland::Server::Surface::subsurfaceTreeChanged,
                         this->qobject.get(),
                         [this] {
                             discard_shape(*this);
                             win::wayland::restack_subsurfaces(this);
                         });
        QObject::connect(surface,
                         &Wrapland::Server::Surface::destroyed,
                         this->qobject.get(),
                         [this] { destroy_window(this); });

        set_surface(this, surface);
        setupCompositing();
    }

    qreal bufferScale() const override
    {
        if (this->remnant) {
            return this->remnant->data.buffer_scale;
        }
        return this->surface->state().scale;
    }

    bool is_wayland_window() const override
    {
        return true;
    }

    void setupCompositing() override
    {
        setup_compositing(*this);
    }

    void add_scene_window_addon() override
    {
        assert(this->surface);
        using scene_t = typename Space::base_t::render_t::compositor_t::scene_t;
        using shadow_t = render::shadow<typename scene_t::window_t>;

        auto setup_buffer = [](auto& buffer) {
            using buffer_integration_t
                = render::wayland::buffer_win_integration<typename scene_t::buffer_t>;

            auto win_integrate = std::make_unique<buffer_integration_t>(buffer);
            auto update_helper = [&buffer]() {
                auto& win_integrate = static_cast<buffer_integration_t&>(*buffer.win_integration);
                update_buffer(*buffer.window->ref_win, win_integrate.external);
            };
            win_integrate->update = update_helper;
            buffer.win_integration = std::move(win_integrate);
        };
        auto get_viewport = [](auto window, auto contentsRect) {
            if (!window->surface) {
                // Can happen on remnant.
                return QRectF();
            }
            if (auto rect = get_scaled_source_rectangle(*window); rect.isValid()) {
                return rect;
            }

            auto buffer = window->surface->state().buffer;
            if (buffer) {
                // Try to get the source rectangle from the buffer size, what defines the source
                // size without respect to destination size.
                auto const origin = contentsRect.topLeft();
                auto const rect = QRectF(origin, buffer->size() - QSize(origin.x(), origin.y()));
                assert(rect.isValid());
                return rect;
            }

            return QRectF();
        };

        this->render->win_integration.setup_buffer = setup_buffer;
        this->render->win_integration.get_viewport = get_viewport;
        this->render->shadow_windowing.create
            = render::wayland::create_shadow<shadow_t, typename scene_t::window_t>;
        this->render->shadow_windowing.update = render::wayland::update_shadow<shadow_t>;

        setup_scale_scene_notify(*this);
    }

    NET::WindowType windowType() const override
    {
        return window_type;
    }

    QByteArray windowRole() const override
    {
        return QByteArray();
    }

    double opacity() const override
    {
        if (this->remnant) {
            return this->remnant->data.opacity;
        }
        if (this->transient->lead() && this->transient->annexed) {
            return this->transient->lead()->opacity();
        }
        return m_opacity;
    }

    void setOpacity(double opacity) override
    {
        assert(this->control);

        opacity = qBound(0.0, opacity, 1.0);
        if (opacity == m_opacity) {
            return;
        }

        auto const old_opacity = m_opacity;
        m_opacity = opacity;

        add_full_repaint(*this);
        Q_EMIT this->qobject->opacityChanged(old_opacity);
    }

    bool isShown() const override
    {
        if (closing || hidden || this->remnant) {
            return false;
        }
        if (!this->control && !this->transient->lead()) {
            return false;
        }

        if (auto lead = this->transient->lead()) {
            if (!lead->isShown()) {
                return false;
            }
        }
        if (this->control && this->control->minimized) {
            return false;
        }
        return this->surface->state().buffer.get();
    }

    bool isHiddenInternal() const override
    {
        if (this->remnant) {
            return false;
        }
        if (auto lead = this->transient->lead()) {
            if (!lead->isHiddenInternal()) {
                return false;
            }
        }
        return hidden || !this->surface->state().buffer;
    }

    QSize minSize() const override
    {
        return this->control->rules.checkMinSize(toplevel->minimumSize());
    }

    QSize maxSize() const override
    {
        return this->control->rules.checkMaxSize(toplevel->maximumSize());
    }

    /// Ask client to provide buffer adapted to new geometry @param rect (in global coordinates).
    void configure_geometry(QRect const& frame_geo)
    {
        // The window geometry relevant to clients is the frame geometry without decorations.
        auto window_geo = frame_geo;

        if (window_geo.isValid()) {
            window_geo = window_geo - frame_margins(this);
        }

        synced_geometry.window = window_geo;
        synced_geometry.max_mode = this->geo.update.max_mode;
        synced_geometry.fullscreen = this->geo.update.fullscreen;

        uint64_t serial = 0;

        if (toplevel) {
            serial = toplevel->configure(xdg_surface_states(this), window_geo.size());
        }
        if (popup) {
            auto parent = this->transient->lead();
            if (parent) {
                auto const top_lead = lead_of_annexed_transient(this);
                auto const bounds = space_window_area(this->space,
                                                      top_lead->control->fullscreen ? FullScreenArea
                                                                                    : PlacementArea,
                                                      top_lead);

                serial = popup->configure(
                    get_xdg_shell_popup_placement(this, bounds).translated(-top_lead->geo.pos()));
            }
        }
        if (layer_surface) {
            serial = layer_surface->configure(window_geo.size());
        }

        configure_event ce;
        ce.serial = serial;
        ce.geometry.frame = frame_geo;
        ce.geometry.max_mode = synced_geometry.max_mode;
        ce.geometry.fullscreen = synced_geometry.fullscreen;
        pending_configures.push_back(ce);
    }

    void apply_pending_geometry()
    {
        assert(toplevel || popup || layer_surface);

        auto frame_geo = this->geo.frame;
        auto position = this->geo.pos();
        auto max_mode = this->max_mode;
        auto fullscreen = this->control ? this->control->fullscreen : false;

        auto serial_match{false};

        for (auto it = pending_configures.begin(); it != pending_configures.end(); it++) {
            if (it->serial > acked_configure) {
                // Serial not acked yet that means all future serials are not.
                // TODO(romangg): can this even happen?
                break;
            }

            if (it->serial == acked_configure) {
                serial_match = true;
                frame_geo = it->geometry.frame;
                position = it->geometry.frame.topLeft();
                max_mode = it->geometry.max_mode;
                fullscreen = it->geometry.fullscreen;

                // Removes all previous pending configures including this one.
                pending_configures.erase(pending_configures.begin(), ++it);
                break;
            }
        }

        if (layer_surface) {
            do_set_geometry(frame_geo);
            return;
        }

        auto const ref_geo = shell_surface->window_geometry();
        frame_geo = QRect(position, ref_geo.size() + frame_size(this));

        if (frame_geo == this->geo.frame && !serial_match
            && this->geo.client_frame_extents == shell_surface->window_margins()) {
            return;
        }

        if (!synced_geometry.window.isValid()) {
            // On first commit.
            synced_geometry.window = ref_geo;
            this->geo.update.frame = frame_geo;
        }

        this->geo.client_frame_extents = shell_surface->window_margins();

        if (popup) {
            auto const toplevel = lead_of_annexed_transient(this);

            if (plasma_shell_surface && isInitialPositionSet()) {
                // Sets position explicitly.
                do_set_geometry(frame_geo);
                discard_shape(*toplevel);
                return;
            }

            auto const screen_bounds
                = space_window_area(this->space,
                                    toplevel->control->fullscreen ? FullScreenArea : PlacementArea,
                                    toplevel);

            // Need to set that for get_xdg_shell_popup_placement(..) call.
            // TODO(romangg): make this less akward, i.e. if possible include it in the call.
            if (this->geo.update.pending == pending_geometry::none) {
                this->geo.update.frame = frame_geo;
            }

            auto const frame_geo = get_xdg_shell_popup_placement(this, screen_bounds);

            if (this->geo.update.pending == win::pending_geometry::none) {
                this->geo.update.frame = frame_geo;
            }
            do_set_geometry(frame_geo);

            discard_shape(*toplevel);
            return;
        }

        if (is_resize(this)) {
            // Adjust the geometry according to the resize process.
            // We must adjust frame geometry because configure events carry the maximum window
            // geometry size. A client with aspect ratio can attach a buffer with smaller size than
            // the one in a configure event.
            auto& mov_res = this->control->move_resize;

            switch (mov_res.contact) {
            case position::top_left:
                frame_geo.moveRight(mov_res.geometry.right());
                frame_geo.moveBottom(mov_res.geometry.bottom());
                break;
            case position::top:
            case position::top_right:
                frame_geo.moveLeft(mov_res.geometry.left());
                frame_geo.moveBottom(mov_res.geometry.bottom());
                break;
            case position::right:
            case position::bottom_right:
            case position::bottom:
                frame_geo.moveLeft(mov_res.geometry.left());
                frame_geo.moveTop(mov_res.geometry.top());
                break;
            case position::bottom_left:
            case position::left:
                frame_geo.moveRight(mov_res.geometry.right());
                frame_geo.moveTop(mov_res.geometry.top());
                break;
            case position::center:
                Q_UNREACHABLE();
            }
        }

        do_set_geometry(frame_geo);
        do_set_fullscreen(fullscreen);
        do_set_maximize_mode(max_mode);
    }

    void do_set_geometry(QRect const& frame_geo)
    {
        auto const old_frame_geo = this->geo.frame;

        if (old_frame_geo == frame_geo) {
            return;
        }

        this->geo.frame = frame_geo;

        if (this->geo.update.pending == win::pending_geometry::none) {
            this->geo.update.frame.setSize(frame_geo.size());
        }

        // TODO(romangg): When we have support for explicit/implicit popup repositioning combine.
        if (old_frame_geo.size() == frame_geo.size()) {
            move_annexed_children(this, frame_geo.topLeft() - old_frame_geo.topLeft());
        } else if (!this->transient->annexed) {
            reposition_annexed_children(this);
        }

        if (old_frame_geo.size() != frame_geo.size()) {
            discard_shape(*this);
        }
        if (plasma_shell_surface && popup) {
            // Plasma-shell surfaces can be xdg-shell popups at the same time. So their geometry
            // might change but they are also annexed. We have to discard the parent window's quads
            // here.
            auto lead = lead_of_annexed_transient(this);
            discard_shape(*lead);
        }

        if (!this->control) {
            add_layer_repaint(*this, visible_rect(this, old_frame_geo));
            add_layer_repaint(*this, visible_rect(this, frame_geo));
            Q_EMIT this->qobject->frame_geometry_changed(old_frame_geo);
            return;
        }

        this->updateWindowRules(rules::type::position | rules::type::size);

        if (is_resize(this)) {
            perform_move_resize(this);
        }

        add_layer_repaint(*this, visible_rect(this, old_frame_geo));
        add_layer_repaint(*this, visible_rect(this, frame_geo));

        Q_EMIT this->qobject->frame_geometry_changed(old_frame_geo);

        // Must be done after signal is emitted so the screen margins are updated.
        if (hasStrut()) {
            update_space_areas(this->space);
        }
    }

    void map()
    {
        if (mapped || !isShown()) {
            return;
        }

        handle_shown_and_mapped();
    }

    void unmap()
    {
        assert(!isShown());

        if (!mapped) {
            return;
        }

        mapped = false;

        if (this->transient->annexed) {
            discard_shape(*this);
        }

        if (this->control) {
            if (this->control->move_resize.enabled) {
                this->leaveMoveResize();
            }
            this->control->destroy_plasma_wayland_integration();
        }

        this->space.base.render->compositor->addRepaint(visible_rect(this));

        if (this->control) {
            process_window_hidden(this->space, this);
        }

        Q_EMIT this->qobject->windowHidden();
    }

    void ping(ping_reason reason)
    {
        assert(toplevel);

        auto serial = this->space.xdg_shell->ping(toplevel->client());
        pings.insert({serial, reason});
    }

    // When another window is created, checks if this window is a subsurface for it.
    void checkTransient(Toplevel<Space>* window) override
    {
        if (this->remnant) {
            return;
        }
        if (this->transient->lead()) {
            // This already has a parent set, we can only set one once.
            return;
        }
        if (!this->surface->subsurface()) {
            // This is not a subsurface.
            return;
        }
        if (this->surface->subsurface()->parentSurface() != window->surface) {
            // This has a parent different to window.
            return;
        }

        // The window is a new parent of this.
        set_subsurface_parent(this, window);

        map();
    }

    void debug(QDebug& stream) const override
    {
        if (this->remnant) {
            stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
            return;
        }

        std::string type = "role unknown";
        if (this->control) {
            type = "toplevel";
        } else if (this->transient->lead()) {
            type = popup ? "popup" : "subsurface";
        }
        if (input_method_popup) {
            type = "input method popup";
        }

        stream.nospace();
        stream << "\'wayland::window"
               << "(" << QString::fromStdString(type) << "):" << this->surface << ";"
               << static_cast<void const*>(this) << "\'";
    }

    win::maximize_mode maximizeMode() const override
    {
        return max_mode;
    }

    bool noBorder() const override
    {
        if (this->remnant) {
            return this->remnant->data.no_border;
        }

        if (xdg_deco
            && xdg_deco->requestedMode() != Wrapland::Server::XdgDecoration::Mode::ClientSide) {
            return !this->space.deco->hasPlugin() || user_no_border || this->geo.update.fullscreen;
        }
        return true;
    }

    void setFullScreen(bool full, bool user = true) override
    {
        update_fullscreen(this, full, user);
    }

    void handle_update_fullscreen(bool full) override
    {
        if (full) {
            update_fullscreen_enable(this);
        } else {
            update_fullscreen_disable(this);
        }
    }

    void setNoBorder(bool set) override
    {
        if (!userCanSetNoBorder()) {
            return;
        }

        set = this->control->rules.checkNoBorder(set);
        if (user_no_border == set) {
            return;
        }

        user_no_border = set;
        updateDecoration(true, false);
        this->updateWindowRules(rules::type::no_border);
    }

    void handle_update_no_border() override
    {
        auto no_border = this->geo.update.max_mode == maximize_mode::full;
        setNoBorder(this->control->rules.checkNoBorder(no_border));
    }

    void updateDecoration(bool check_workspace_pos, bool force = false) override
    {
        if ((!win::decoration(this) && noBorder()) || (win::decoration(this) && !noBorder())) {
            if (!force) {
                return;
            }
        }

        auto const old_geom = this->geo.frame;
        auto const old_content_geom = old_geom.adjusted(
            left_border(this), top_border(this), -right_border(this), -bottom_border(this));

        block_geometry_updates(this, true);

        if (force) {
            this->control->destroy_decoration();
        }

        if (noBorder()) {
            this->control->destroy_decoration();
        } else {
            // Create decoration.
            this->control->deco.window = new deco::window<Toplevel<Space>>(this);
            auto decoration = this->space.deco->createDecoration(this->control->deco.window);
            if (decoration) {
                QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);
                QObject::connect(decoration,
                                 &KDecoration2::Decoration::shadowChanged,
                                 this->qobject.get(),
                                 [this] { update_shadow(this); });
                QObject::connect(decoration,
                                 &KDecoration2::Decoration::bordersChanged,
                                 this->qobject.get(),
                                 [this]() {
                                     geometry_updates_blocker geo_blocker(this);
                                     auto const old_geom = this->geo.frame;
                                     check_workspace_position(this, old_geom);
                                     Q_EMIT this->qobject->frame_geometry_changed(old_geom);
                                 });
            }

            this->control->deco.decoration = decoration;
            auto const deco_size = QSize(left_border(this) + right_border(this),
                                         bottom_border(this) + top_border(this));

            // TODO: ensure the new geometry still fits into the client area (e.g. maximized
            // windows)
            // TODO(romangg): use setFrameGeometry?
            do_set_geometry(QRect(old_geom.topLeft(), this->geo.size() + deco_size));
            Q_EMIT this->qobject->frame_geometry_changed(old_geom);
        }

        if (xdg_deco) {
            auto const mode = (win::decoration(this) || user_no_border)
                ? Wrapland::Server::XdgDecoration::Mode::ServerSide
                : Wrapland::Server::XdgDecoration::Mode::ClientSide;
            xdg_deco->configure(mode);
        }

        update_shadow(this);

        if (check_workspace_pos) {
            check_workspace_position(this, old_geom, -2, old_content_geom);
        }

        block_geometry_updates(this, false);
    }

    void takeFocus() override
    {
        assert(this->control);

        if (this->control->rules.checkAcceptFocus(wantsInput())) {
            if (toplevel) {
                ping(ping_reason::focus);
            }
            set_active(this, true);
        }

        if (!this->control->keep_above && !is_on_screen_display(this) && !belongsToDesktop()) {
            set_showing_desktop(this->space, false);
        }
    }

    bool userCanSetFullScreen() const override
    {
        return this->control.get();
    }

    bool userCanSetNoBorder() const override
    {
        if (!this->space.deco->hasPlugin()) {
            return false;
        }
        if (!xdg_deco
            || xdg_deco->requestedMode() == Wrapland::Server::XdgDecoration::Mode::ClientSide) {
            return false;
        }
        return !this->control->fullscreen;
    }

    bool wantsInput() const override
    {
        assert(this->control);

        if (layer_surface) {
            return layer_surface->keyboard_interactivity()
                == Wrapland::Server::LayerSurfaceV1::KeyboardInteractivity::OnDemand;
        }
        return this->control->rules.checkAcceptFocus(acceptsFocus());
    }

    bool dockWantsInput() const override
    {
        if (plasma_shell_surface
            && plasma_shell_surface->role() == Wrapland::Server::PlasmaShellSurface::Role::Panel) {
            return plasma_shell_surface->panelTakesFocus();
        }
        if (layer_surface
            && layer_surface->keyboard_interactivity()
                != Wrapland::Server::LayerSurfaceV1::KeyboardInteractivity::None) {
            return true;
        }
        return false;
    }

    bool has_exclusive_keyboard_interactivity() const
    {
        return layer_surface
            && layer_surface->keyboard_interactivity()
            == Wrapland::Server::LayerSurfaceV1::KeyboardInteractivity::Exclusive;
    }

    bool hasStrut() const override
    {
        if (!isShown()) {
            return false;
        }
        if (plasma_shell_surface) {
            using PSS = Wrapland::Server::PlasmaShellSurface;
            return plasma_shell_surface->role() == PSS::Role::Panel
                && plasma_shell_surface->panelBehavior() == PSS::PanelBehavior::AlwaysVisible;
        }
        if (layer_surface) {
            return layer_surface->exclusive_zone() > 0;
        }
        return false;
    }

    pid_t pid() const override
    {
        if (this->remnant || !this->surface->client()) {
            return 0;
        }
        return this->surface->client()->processId();
    }

    bool isLockScreen() const override
    {
        return !this->remnant
            && this->surface->client() == waylandServer()->screen_locker_client_connection;
    }

    bool isInitialPositionSet() const override
    {
        if (layer_surface) {
            return true;
        }
        return plasma_shell_surface
            && (plasma_shell_surface->isPositionSet() || plasma_shell_surface->open_under_cursor());
    }

    void showOnScreenEdge() override
    {
        if (!plasma_shell_surface || !mapped) {
            return;
        }

        hideClient(false);
        raise_window(&this->space, this);

        if (plasma_shell_surface->panelBehavior()
            == Wrapland::Server::PlasmaShellSurface::PanelBehavior::AutoHide) {
            plasma_shell_surface->showAutoHidingPanel();
        }
    }

    void cancel_popup()
    {
        assert(popup);
        if (popup) {
            popup->popupDone();
        }
    }

    void closeWindow() override
    {
        assert(isCloseable());

        if (isCloseable()) {
            toplevel->close();
            ping(ping_reason::close);
        }
    }

    bool isCloseable() const override
    {
        return toplevel && window_type != NET::Desktop && window_type != NET::Dock;
    }

    bool isMaximizable() const override
    {
        if (!isResizable()) {
            return false;
        }

        return this->control->rules.checkMaximize(maximize_mode::restore) == maximize_mode::restore
            && this->control->rules.checkMaximize(maximize_mode::full) == maximize_mode::full;
    }

    bool isMinimizable() const override
    {
        if (!this->control) {
            return false;
        }
        if (layer_surface) {
            return false;
        }
        if (!this->control->rules.checkMinimize(true)) {
            return false;
        }
        return (!plasma_shell_surface
                || plasma_shell_surface->role()
                    == Wrapland::Server::PlasmaShellSurface::Role::Normal);
    }

    bool isMovable() const override
    {
        if (!this->control) {
            return false;
        }
        if (layer_surface) {
            return false;
        }
        if (this->geo.update.fullscreen) {
            return false;
        }
        if (this->control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
            return false;
        }
        if (plasma_shell_surface) {
            return plasma_shell_surface->role()
                == Wrapland::Server::PlasmaShellSurface::Role::Normal;
        }
        return true;
    }

    bool isMovableAcrossScreens() const override
    {
        if (!this->control) {
            return false;
        }
        if (layer_surface) {
            return false;
        }
        if (is_special_window(this) && !is_splash(this) && !is_toolbar(this)
            && !is_applet_popup(this)) {
            return false;
        }
        if (this->control->rules.checkPosition(geo::invalid_point) != geo::invalid_point) {
            return false;
        }
        if (plasma_shell_surface) {
            return plasma_shell_surface->role()
                == Wrapland::Server::PlasmaShellSurface::Role::Normal;
        }
        return true;
    }

    bool isResizable() const override
    {
        if (!this->control) {
            return false;
        }
        if (layer_surface) {
            return false;
        }
        if (this->geo.update.fullscreen) {
            return false;
        }
        if (this->control->rules.checkSize(QSize()).isValid()) {
            return false;
        }
        if (plasma_shell_surface) {
            using Role = Wrapland::Server::PlasmaShellSurface::Role;
            auto role = plasma_shell_surface->role();
            return role == Role::Normal || role == Role::AppletPopup;
        }

        assert(toplevel);
        auto const min = minSize();
        auto const max = maxSize();

        return min.width() < max.width() || min.height() < max.height();
    }

    void hideClient(bool hide) override
    {
        auto const was_shown = isShown();

        if (hidden == hide) {
            return;
        }

        hidden = hide;

        if (was_shown == isShown()) {
            return;
        }

        if (hide) {
            this->space.base.render->compositor->addRepaint(visible_rect(this));
            process_window_hidden(this->space, this);
            Q_EMIT this->qobject->windowHidden();
        } else {
            handle_shown_and_mapped();
        }
    }

    void placeIn(const QRect& area)
    {
        win::place(this, area);
    }

    void update_maximized(maximize_mode mode) override
    {
        // TODO(romangg): If this window is fullscreen it should still be possible to set it
        //                maximized, but without changing the geometry just right now.
        win::update_maximized(this, mode);
    }

    void doResizeSync() override
    {
        configure_geometry(this->control->move_resize.geometry);
    }

    bool belongsToSameApplication(Toplevel<Space> const* other,
                                  win::same_client_check checks) const override
    {
        if (flags(checks & win::same_client_check::allow_cross_process)) {
            if (other->control->desktop_file_name == this->control->desktop_file_name) {
                return true;
            }
        }
        if (auto s = other->surface) {
            return s->client() == this->surface->client();
        }
        return false;
    }

    bool belongsToDesktop() const override
    {
        auto const windows = this->space.windows;

        return std::any_of(windows.cbegin(), windows.cend(), [this](auto const& win) {
            if (belongsToSameApplication(win, flags<same_client_check>())) {
                return is_desktop(win);
            }
            return false;
        });
    }

    void doSetActive() override
    {
        assert(this->control);

        if (!this->control->active) {
            return;
        }
        blocker block(this->space.stacking.order);
        focus_to_null(this->space);
    }

    void doMinimize() override
    {
        if (this->control->minimized) {
            process_window_hidden(this->space, this);
        } else {
            Q_EMIT this->qobject->windowShown();
        }
        propagate_minimized_to_transients(*this);
    }

    void setFrameGeometry(QRect const& rect) override
    {
        auto const frame_geo = this->control ? this->control->rules.checkGeometry(rect) : rect;

        this->geo.update.frame = frame_geo;

        if (this->geo.update.block) {
            this->geo.update.pending = win::pending_geometry::normal;
            return;
        }

        this->geo.update.pending = pending_geometry::none;

        if (needs_configure(this)) {
            if (plasma_shell_surface) {
                if (!pending_configures.empty()) {
                    pending_configures.back().geometry.frame.moveTo(frame_geo.topLeft());
                }
                do_set_geometry(QRect(frame_geo.topLeft(), this->geo.size()));
            }
            configure_geometry(frame_geo);
            return;
        }

        assert(synced_geometry.max_mode == this->geo.update.max_mode);
        assert(synced_geometry.fullscreen == this->geo.update.fullscreen);

        if (!pending_configures.empty()) {
            // We might be here with a new position but a size not yet acked by the client.
            // Therefore only set the new position and keep the current frame size.
            pending_configures.back().geometry.frame.moveTo(frame_geo.topLeft());
            return;
        }

        do_set_geometry(frame_geo);
    }

    void apply_restore_geometry(QRect const& restore_geo) override
    {
        auto rectified_geo = rectify_restore_geometry(this, restore_geo);

        if (!restore_geo.isValid()) {
            // When the restore geometry was not valid we let the client send a new size instead of
            // using the one determined by our rectify function.
            // TODO(romangg): This can offset the relative Placement, e.g. when centered. Place
            //                again later on when we received the new size from client?
            rectified_geo.setSize(QSize());
        }

        setFrameGeometry(rectified_geo);
    }

    void restore_geometry_from_fullscreen() override
    {
        assert(!has_special_geometry_mode_besides_fullscreen(this));

        // In case the restore geometry is invalid, use the placement from the rectify function.
        auto restore_geo = rectify_fullscreen_restore_geometry(this);

        if (!this->geo.restore.max.isValid()) {
            // We let the client decide on a size.
            restore_geo.setSize(QSize(0, 0));
        }

        setFrameGeometry(restore_geo);
        this->geo.restore.max = {};
    }

    win::layer layer_for_dock() const override
    {
        assert(this->control);

        if (!plasma_shell_surface) {
            return win::layer_for_dock(*this);
        }

        using PSS = Wrapland::Server::PlasmaShellSurface;

        switch (plasma_shell_surface->panelBehavior()) {
        case PSS::PanelBehavior::WindowsCanCover:
            return layer::normal;
        case PSS::PanelBehavior::AutoHide:
        case PSS::PanelBehavior::WindowsGoBelow:
            return layer::above;
        case PSS::PanelBehavior::AlwaysVisible:
            return layer::dock;
        default:
            Q_UNREACHABLE();
            break;
        }

        return layer::unknown;
    }

    bool has_pending_repaints() const override
    {
        return this->ready_for_painting && Toplevel<Space>::has_pending_repaints();
    }

    void updateColorScheme() override
    {
        assert(this->control);

        if (palette) {
            set_color_scheme(this, this->control->rules.checkDecoColor(palette->palette()));
        } else {
            set_color_scheme(this, this->control->rules.checkDecoColor(QString()));
        }
    }

    bool isInputMethod() const override
    {
        return input_method_popup;
    }

    bool is_popup_end() const override
    {
        return this->remnant ? this->remnant->data.was_popup_window : static_cast<bool>(popup);
    }

    void killWindow() override
    {
        auto client = this->surface->client();
        if (client->processId() == getpid() || client->processId() == 0) {
            client->destroy();
            return;
        }

        ::kill(client->processId(), SIGTERM);

        // Give it time to terminate. Only if terminate fails try destroying the Wayland connection.
        QTimer::singleShot(5000, client, &Wrapland::Server::Client::destroy);
    }

    bool supportsWindowRules() const override
    {
        return toplevel && !plasma_shell_surface;
    }

    void handle_class_changed()
    {
        auto const window_class = QByteArray(toplevel->appId().c_str());
        set_wm_class(*this, this->wm_class.res_name, window_class);
        if (initialized && supportsWindowRules()) {
            rules::setup_rules(this, true);
            this->applyWindowRules();
        }
        set_desktop_file_name(this, window_class);
    }

    void handle_title_changed()
    {
        auto const old_suffix = this->caption.suffix;

        this->caption.normal = QString::fromStdString(toplevel->title()).simplified();
        updateCaption();

        if (this->caption.suffix == old_suffix) {
            // Don't emit caption change twice it already got emitted by the changing suffix.
            Q_EMIT this->qobject->captionChanged();
        }
    }

    bool initialized{false};
    NET::WindowType window_type{NET::Normal};

    bool user_no_border{false};

    bool hidden{false};
    bool mapped{false};
    bool closing{false};

    double m_opacity = 1.0;

    struct configure_event {
        uint32_t serial{0};

        // Geometry to apply after a resize operation has been completed.
        struct {
            QRect frame;
            maximize_mode max_mode{maximize_mode::restore};
            bool fullscreen{false};
        } geometry;
    };
    std::vector<configure_event> pending_configures;

    void handle_commit()
    {
        if (!this->surface->state().buffer) {
            unmap();
            return;
        }

        if (this->surface->state().updates & Wrapland::Server::surface_change::size) {
            discard_buffer(*this);
        }

        if (auto const& damage = this->surface->state().damage; !damage.isEmpty()) {
            handle_surface_damage(*this, damage);
        } else if (this->surface->state().updates & Wrapland::Server::surface_change::frame) {
            this->space.base.render->compositor->schedule_frame_callback(this);
        }

        if (toplevel || popup) {
            apply_pending_geometry();

            // Plasma surfaces might set position late. So check again initial position being set.
            if (must_place) {
                if (!isInitialPositionSet()) {
                    must_place = false;
                    auto const area = space_window_area(this->space,
                                                        PlacementArea,
                                                        get_current_output(this->space),
                                                        this->desktop());
                    placeIn(area);
                } else if (plasma_shell_surface && plasma_shell_surface->open_under_cursor()) {
                    must_place = false;
                    auto const area = space_window_area(this->space,
                                                        PlacementArea,
                                                        this->space.input->cursor->pos(),
                                                        this->desktop());
                    auto size = this->geo.size();
                    auto pos = this->space.input->cursor->pos()
                        - QPoint(size.width(), size.height()) / 2;
                    win::move(this, pos);
                    win::keep_in_area(this, area, false);
                }
            }
        } else if (layer_surface) {
            handle_layer_surface_commit(this);
            apply_pending_geometry();
        } else if (auto cur_size = client_to_frame_size(this, this->surface->size());
                   this->geo.size() != cur_size) {
            do_set_geometry(QRect(this->geo.pos(), cur_size));
        }

        auto bit_depth
            = (this->surface->state().buffer->hasAlphaChannel() && !is_desktop(this)) ? 32 : 24;
        set_bit_depth(*this, bit_depth);
        map();
    }

    void do_set_maximize_mode(win::maximize_mode mode)
    {
        if (mode == max_mode) {
            return;
        }

        auto old_mode = max_mode;
        max_mode = mode;

        this->updateWindowRules(rules::type::maximize_horiz | rules::type::maximize_vert
                                | rules::type::position | rules::type::size);

        // Update decoration borders.
        if (auto deco = decoration(this); deco && deco->client()
            && !(kwinApp()->options->qobject->borderlessMaximizedWindows()
                 && mode == maximize_mode::full)) {
            auto const deco_client = win::decoration(this)->client().toStrongRef();
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
        }

        // Active fullscreens gets a different layer.
        update_layer(this);

        this->updateWindowRules(rules::type::fullscreen | rules::type::position
                                | rules::type::size);
        Q_EMIT this->qobject->fullScreenChanged();
    }

    bool acceptsFocus() const override
    {
        assert(this->control);

        using PSS = Wrapland::Server::PlasmaShellSurface;

        if (plasma_shell_surface) {
            if (plasma_shell_surface->role() == PSS::Role::OnScreenDisplay
                || plasma_shell_surface->role() == PSS::Role::ToolTip) {
                return false;
            }

            if (plasma_shell_surface->role() == PSS::Role::Notification
                || plasma_shell_surface->role() == PSS::Role::CriticalNotification) {
                return plasma_shell_surface->panelTakesFocus();
            }
        }
        if (!mapped || closing) {
            return false;
        }
        return true;
    }

    void updateCaption() override
    {
        auto const old_suffix = this->caption.suffix;
        auto const shortcut = shortcut_caption_suffix(this);
        this->caption.suffix = shortcut;
        if ((!is_special_window(this) || is_toolbar(this))
            && find_client_with_same_caption(static_cast<Toplevel<Space>*>(this))) {
            int i = 2;
            do {
                this->caption.suffix
                    = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
                i++;
            } while (find_client_with_same_caption(static_cast<Toplevel<Space>*>(this)));
        }
        if (this->caption.suffix != old_suffix) {
            Q_EMIT this->qobject->captionChanged();
        }
    }

    maximize_mode max_mode{maximize_mode::restore};

    struct {
        QRect window;
        maximize_mode max_mode{maximize_mode::restore};
        bool fullscreen{false};
    } synced_geometry;

    Wrapland::Server::XdgShellSurface* shell_surface{nullptr};
    Wrapland::Server::XdgShellToplevel* toplevel{nullptr};
    Wrapland::Server::XdgShellPopup* popup{nullptr};
    Wrapland::Server::LayerSurfaceV1* layer_surface{nullptr};
    Wrapland::Server::input_method_popup_surface_v2* input_method_popup{nullptr};

    Wrapland::Server::XdgDecoration* xdg_deco{nullptr};
    Wrapland::Server::PlasmaShellSurface* plasma_shell_surface{nullptr};
    Wrapland::Server::ServerSideDecorationPalette* palette{nullptr};

    std::map<uint32_t, ping_reason> pings;
    uint32_t acked_configure{0};

    bool must_place{false};
    bool inhibit_idle{false};

private:
    void handle_shown_and_mapped()
    {
        mapped = true;

        if (this->transient->annexed) {
            discard_shape(*this);
        }

        if (this->control) {
            if (!isLockScreen()) {
                setup_plasma_management(&this->space, this);
            }
            update_screen_edge(this);
        }

        if (this->ready_for_painting) {
            // Was already shown in the past once. Just repaint and emit shown again.
            add_full_repaint(*this);
            Q_EMIT this->qobject->windowShown();
            return;
        }

        // First time shown. Must be added to space.
        set_ready_for_painting(*this);
        this->space.handle_window_added(this);
    }
};

}
