/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"
#include "base/x11/xcb/window.h"
#include "input/cursor.h"
#include "render/window.h"
#include "win/activation.h"
#include "win/control.h"
#include "win/remnant.h"
#include "win/rules/ruling.h"
#include "win/rules/update.h"
#include "win/shortcut_set.h"
#include "win/virtual_desktops.h"
#include "win/window_qobject.h"
#include "win/x11/group.h"

#include <NETWM>
#include <QMatrix4x4>
#include <QUuid>
#include <functional>
#include <memory>
#include <optional>
#include <xcb/damage.h>
#include <xcb/xfixes.h>

namespace KWin
{

namespace win::x11
{
class client_machine;
}

template<typename Space>
class Toplevel
{
public:
    constexpr static bool is_toplevel{true};

    using space_t = Space;
    using type = Toplevel<space_t>;
    using qobject_t = win::window_qobject;
    using render_t = render::window<type>;
    using output_t = typename space_t::base_t::output_t;

    std::unique_ptr<qobject_t> qobject;
    std::unique_ptr<render_t> render;

    struct {
        QString normal;
        // suffix added to normal caption (e.g. shortcut, machine name, etc.).
        QString suffix;
    } caption;

    // Always lowercase
    QByteArray resource_name;
    QByteArray resource_class;

    struct {
        int block{0};
        win::pending_geometry pending{win::pending_geometry::none};

        QRect frame;
        win::maximize_mode max_mode{win::maximize_mode::restore};
        bool fullscreen{false};

        struct {
            QMargins deco_margins;
            QMargins client_frame_extents;
        } original;
    } geometry_update;

    struct {
        QMetaObject::Connection frame_update_outputs;
        QMetaObject::Connection screens_update_outputs;
        QMetaObject::Connection check_screen;
    } notifiers;

    /**
     * Used to store and retrieve frame geometry values when certain geometry-transforming
     * actions are triggered and later reversed again. For example when a window has been
     * maximized and later again unmaximized.
     */
    struct {
        QRect maximize;
    } restore_geometries;

    // Relative to client geometry.
    QRegion damage_region;
    xcb_damage_damage_t damage_handle{XCB_NONE};

    // Relative to frame geometry.
    QRegion repaints_region;
    QRegion layer_repaints_region;
    bool ready_for_painting{false};
    bool m_isDamaged{false};
    bool is_shape{false};

    /// Area to be opaque. Only provides valuable information if hasAlpha is @c true.
    QRegion opaque_region;

    output_t const* central_output{nullptr};

    /**
     * Records all outputs that still need to be repainted for the current repaint regions.
     */
    std::vector<output_t*> repaint_outputs;
    Space& space;

    virtual ~Toplevel()
    {
        space.windows_map.erase(signal_id);
        delete info;
    }

    virtual xcb_window_t frameId() const
    {
        if (remnant) {
            return remnant->data.frame;
        }
        return xcb_window;
    }

    QRegion render_region() const
    {
        if (remnant) {
            return remnant->data.render_region;
        }

        auto const render_geo = win::render_geometry(this);

        if (is_shape) {
            if (m_render_shape_valid) {
                return m_render_shape;
            }
            m_render_shape_valid = true;
            m_render_shape = QRegion();

            auto cookie = xcb_shape_get_rectangles_unchecked(
                connection(), frameId(), XCB_SHAPE_SK_BOUNDING);
            unique_cptr<xcb_shape_get_rectangles_reply_t> reply(
                xcb_shape_get_rectangles_reply(connection(), cookie, nullptr));
            if (!reply) {
                return QRegion();
            }

            auto const rects = xcb_shape_get_rectangles_rectangles(reply.get());
            auto const rect_count = xcb_shape_get_rectangles_rectangles_length(reply.get());
            for (int i = 0; i < rect_count; ++i) {
                m_render_shape += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
            }

            // make sure the shape is sane (X is async, maybe even XShape is broken)
            m_render_shape &= QRegion(0, 0, render_geo.width(), render_geo.height());
            return m_render_shape;
        }

        return QRegion(0, 0, render_geo.width(), render_geo.height());
    }

    void discard_shape()
    {
        m_render_shape_valid = false;
        discard_quads();
    }

    void discard_quads()
    {
        if (render) {
            render->invalidateQuadsCache();
            addRepaintFull();
        }
        if (transient()->annexed) {
            for (auto lead : transient()->leads()) {
                lead->discard_quads();
            }
        }
    }

    /**
     * Returns the geometry of the Toplevel, excluding invisible portions, e.g.
     * server-side and client-side drop shadows, etc.
     */
    QRect frameGeometry() const
    {
        return m_frameGeometry;
    }

    void set_frame_geometry(QRect const& rect)
    {
        m_frameGeometry = rect;
    }

    QSize size() const
    {
        return m_frameGeometry.size();
    }

    QPoint pos() const
    {
        return m_frameGeometry.topLeft();
    }

    /**
     * Returns the ratio between physical pixels and device-independent pixels for
     * the attached buffer (or pixmap).
     *
     * For X11 clients, this method always returns 1.
     */
    virtual qreal bufferScale() const
    {
        return remnant ? remnant->data.buffer_scale : 1.;
    }

    virtual bool is_wayland_window() const
    {
        return false;
    }

    virtual bool isClient() const
    {
        return false;
    }

    virtual NET::WindowType get_window_type_direct() const
    {
        return windowType();
    }

    virtual NET::WindowType windowType() const = 0;

    virtual bool isLockScreen() const
    {
        return false;
    }

    virtual bool isInputMethod() const
    {
        return false;
    }

    /**
     * Returns the virtual desktop within the workspace() the client window
     * is located in, 0 if it isn't located on any special desktop (not mapped yet),
     * or NET::OnAllDesktops. Do not use desktop() directly, use
     * isOnDesktop() instead.
     */
    virtual int desktop() const
    {
        // TODO: for remnant special case?
        return m_desktops.isEmpty() ? static_cast<int>(NET::OnAllDesktops)
                                    : m_desktops.last()->x11DesktopNumber();
    }

    QVector<win::virtual_desktop*> desktops() const
    {
        return m_desktops;
    }

    void set_desktops(QVector<win::virtual_desktop*> const& desktops)
    {
        m_desktops = desktops;
    }

    bool isOnDesktop(int d) const
    {
        return win::on_desktop(this, d);
    }

    bool isOnCurrentDesktop() const
    {
        return win::on_current_desktop(this);
    }

    bool isOnAllDesktops() const
    {
        return win::on_all_desktops(this);
    }

    virtual QByteArray windowRole() const
    {
        if (remnant) {
            return remnant->data.window_role;
        }
        return QByteArray(info->windowRole());
    }

    virtual win::x11::client_machine* get_client_machine() const
    {
        return {};
    }

    virtual QByteArray wmClientMachine(bool /*use_localhost*/) const
    {
        return {};
    }

    virtual bool isLocalhost() const
    {
        return true;
    }

    virtual pid_t pid() const
    {
        return info->pid();
    }

    virtual double opacity() const
    {
        if (remnant) {
            return remnant->data.opacity;
        }
        if (info->opacity() == 0xffffffff)
            return 1.0;
        return info->opacity() * 1.0 / 0xffffffff;
    }

    virtual void setOpacity(double new_opacity)
    {
        double old_opacity = opacity();
        new_opacity = qBound(0.0, new_opacity, 1.0);
        if (old_opacity == new_opacity)
            return;
        info->setOpacity(static_cast<unsigned long>(new_opacity * 0xffffffff));
        if (space.base.render->compositor->scene) {
            addRepaintFull();
            Q_EMIT qobject->opacityChanged(old_opacity);
        }
    }

    bool hasAlpha() const
    {
        return bit_depth == 32;
    }

    virtual bool setupCompositing()
    {
        // Should never be called, always through the child classes instead.
        assert(false);
        return false;
    }

    virtual void add_scene_window_addon()
    {
    }

    virtual void finishCompositing()
    {
        assert(!remnant);

        if (render) {
            discard_buffer();
            render.reset();
        }

        damage_region = QRegion();
        repaints_region = QRegion();
    }

    void addRepaint(QRegion const& region)
    {
        if (!space.base.render->compositor->scene) {
            return;
        }
        repaints_region += region;
        add_repaint_outputs(region.translated(pos()));
        Q_EMIT qobject->needsRepaint();
    }

    void addLayerRepaint(QRegion const& region)
    {
        if (!space.base.render->compositor->scene) {
            return;
        }
        layer_repaints_region += region;
        add_repaint_outputs(region);
        Q_EMIT qobject->needsRepaint();
    }

    virtual void addRepaintFull()
    {
        auto const region = win::visible_rect(this);
        repaints_region = region.translated(-pos());
        for (auto child : transient()->children) {
            if (child->transient()->annexed) {
                child->addRepaintFull();
            }
        }
        add_repaint_outputs(region);
        Q_EMIT qobject->needsRepaint();
    }

    virtual bool has_pending_repaints() const
    {
        return !repaints().isEmpty();
    }

    QRegion repaints() const
    {
        return repaints_region.translated(pos()) | layer_repaints_region;
    }

    void resetRepaints(output_t* output)
    {
        auto reset_all = [this] {
            repaints_region = QRegion();
            layer_repaints_region = QRegion();
        };

        if (!output) {
            assert(!repaint_outputs.size());
            reset_all();
            return;
        }

        remove_all(repaint_outputs, output);

        if (!repaint_outputs.size()) {
            reset_all();
            return;
        }

        auto reset_region = QRegion(output->geometry());

        for (auto out : repaint_outputs) {
            reset_region = reset_region.subtracted(out->geometry());
        }

        repaints_region.translate(pos());
        repaints_region = repaints_region.subtracted(reset_region);
        repaints_region.translate(-pos());

        layer_repaints_region = layer_repaints_region.subtracted(reset_region);
    }

    void resetDamage()
    {
        damage_region = QRegion();
    }

    void addDamageFull()
    {
        if (!space.base.render->compositor->scene) {
            return;
        }

        auto const render_geo = win::frame_to_render_rect(this, frameGeometry());

        auto const damage = QRect(QPoint(), render_geo.size());
        damage_region = damage;

        auto repaint = damage;
        if (has_in_content_deco) {
            repaint.translate(-QPoint(win::left_border(this), win::top_border(this)));
        }
        repaints_region |= repaint;
        add_repaint_outputs(render_geo);

        Q_EMIT qobject->damaged(damage_region);
    }

    // TODO(romangg): * This function is only called on Wayland and the damage translation is not
    //                  the usual way. Unify that.
    //                * Should we return early on the added damage being empty?
    virtual void addDamage(const QRegion& damage)
    {
        auto const render_region = win::render_geometry(this);
        repaints_region += damage.translated(render_region.topLeft() - pos());
        add_repaint_outputs(render_region);

        m_isDamaged = true;
        damage_region += damage;
        Q_EMIT qobject->damaged(damage);
    }

    /**
     * Whether the Toplevel currently wants the shadow to be rendered. Default
     * implementation always returns @c true.
     */
    virtual bool wantsShadowToBeRendered() const
    {
        return true;
    }

    win::layer layer() const
    {
        if (transient()->lead() && transient()->annexed) {
            return transient()->lead()->layer();
        }
        if (m_layer == win::layer::unknown) {
            const_cast<type*>(this)->m_layer = win::belong_to_layer(this);
        }
        return m_layer;
    }

    void set_layer(win::layer layer)
    {
        m_layer = layer;
        ;
    }

    /**
     * Resets the damage state and sends a request for the damage region.
     * A call to this function must be followed by a call to getDamageRegionReply(),
     * or the reply will be leaked.
     *
     * Returns true if the window was damaged, and false otherwise.
     */
    bool resetAndFetchDamage()
    {
        if (!m_isDamaged)
            return false;

        if (damage_handle == XCB_NONE) {
            m_isDamaged = false;
            return true;
        }

        xcb_connection_t* conn = connection();

        // Create a new region and copy the damage region to it,
        // resetting the damaged state.
        xcb_xfixes_region_t region = xcb_generate_id(conn);
        xcb_xfixes_create_region(conn, region, 0, nullptr);
        xcb_damage_subtract(conn, damage_handle, 0, region);

        // Send a fetch-region request and destroy the region
        m_regionCookie = xcb_xfixes_fetch_region_unchecked(conn, region);
        xcb_xfixes_destroy_region(conn, region);

        m_isDamaged = false;
        m_damageReplyPending = true;

        return m_damageReplyPending;
    }

    /**
     * Gets the reply from a previous call to resetAndFetchDamage().
     * Calling this function is a no-op if there is no pending reply.
     * Call damage() to return the fetched region.
     */
    void getDamageRegionReply()
    {
        if (!m_damageReplyPending) {
            return;
        }

        m_damageReplyPending = false;

        // Get the fetch-region reply
        auto reply = xcb_xfixes_fetch_region_reply(connection(), m_regionCookie, nullptr);
        if (!reply) {
            return;
        }

        // Convert the reply to a QRegion. The region is relative to the content geometry.
        auto count = xcb_xfixes_fetch_region_rectangles_length(reply);
        QRegion region;

        if (count > 1 && count < 16) {
            auto rects = xcb_xfixes_fetch_region_rectangles(reply);

            QVector<QRect> qrects;
            qrects.reserve(count);

            for (int i = 0; i < count; i++) {
                qrects << QRect(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
            }
            region.setRects(qrects.constData(), count);
        } else {
            region += QRect(
                reply->extents.x, reply->extents.y, reply->extents.width, reply->extents.height);
        }

        region.translate(-QPoint(client_frame_extents.left(), client_frame_extents.top()));
        repaints_region |= region;

        if (has_in_content_deco) {
            region.translate(-QPoint(win::left_border(this), win::top_border(this)));
        }
        damage_region |= region;

        free(reply);
    }

    bool skipsCloseAnimation() const
    {
        return m_skipCloseAnimation;
    }

    void setSkipCloseAnimation(bool set)
    {
        if (set == m_skipCloseAnimation) {
            return;
        }
        m_skipCloseAnimation = set;
        Q_EMIT qobject->skipCloseAnimationChanged();
    }

    /**
     * Maps from global to window coordinates.
     */
    QMatrix4x4 input_transform() const
    {
        QMatrix4x4 transform;

        auto const render_pos = win::frame_to_render_pos(this, pos());
        transform.translate(-render_pos.x(), -render_pos.y());

        return transform;
    }

    /**
     * Can be implemented by child classes to add additional checks to the ones in win::is_popup.
     */
    virtual bool is_popup_end() const
    {
        if (remnant) {
            return remnant->data.was_popup_window;
        }
        return false;
    }

    virtual win::layer layer_for_dock() const
    {
        assert(control);

        // Slight hack for the 'allow window to cover panel' Kicker setting.
        // Don't move keepbelow docks below normal window, but only to the same
        // layer, so that both may be raised to cover the other.
        if (control->keep_below) {
            return win::layer::normal;
        }
        if (control->keep_above) {
            // slight hack for the autohiding panels
            return win::layer::above;
        }
        return win::layer::dock;
    }

    /**
     * Returns whether this is an internal client.
     *
     * Internal clients are created by KWin and used for special purpose windows,
     * like the task switcher, etc.
     *
     * Default implementation returns @c false.
     */
    virtual bool isInternal() const
    {
        return false;
    }

    virtual bool belongsToDesktop() const = 0;
    virtual void checkTransient(type* window) = 0;

    virtual void damageNotifyEvent()
    {
        m_isDamaged = true;

        // Note: The region is supposed to specify the damage extents,
        //       but we don't know it at this point. No one who connects
        //       to this signal uses the rect however.
        Q_EMIT qobject->damaged({});
    }

    void discard_buffer()
    {
        addDamageFull();
        if (render) {
            render->discard_buffer();
        }
    }

    void setResourceClass(const QByteArray& name, const QByteArray& className = QByteArray())
    {
        resource_name = name;
        resource_class = className;
        Q_EMIT qobject->windowClassChanged();
    }

    /**
     * Checks whether the screen number for this Toplevel changed and updates if needed.
     * Any method changing the geometry of the Toplevel should call this method.
     */
    void checkScreen()
    {
        auto const& outputs = space.base.outputs;
        auto output = base::get_nearest_output(outputs, frameGeometry().center());
        if (central_output != output) {
            auto old_out = central_output;
            central_output = output;
            Q_EMIT qobject->central_output_changed(old_out, output);
        }
    }

    void setupCheckScreenConnection()
    {
        notifiers.check_screen = QObject::connect(qobject.get(),
                                                  &win::window_qobject::frame_geometry_changed,
                                                  qobject.get(),
                                                  [this] { checkScreen(); });
        checkScreen();
    }

    void removeCheckScreenConnection()
    {
        QObject::disconnect(notifiers.check_screen);
    }

    void setReadyForPainting()
    {
        if (!ready_for_painting) {
            ready_for_painting = true;
            if (space.base.render->compositor->scene) {
                addRepaintFull();
                Q_EMIT qobject->windowShown();
            }
        }
    }

    void handle_output_added(output_t* output)
    {
        if (!central_output) {
            central_output = output;
            Q_EMIT qobject->central_output_changed(nullptr, output);
            return;
        }

        checkScreen();
    }

    void handle_output_removed(output_t* output)
    {
        if (central_output != output) {
            return;
        }
        auto const& outputs = space.base.outputs;
        central_output = base::get_nearest_output(outputs, frameGeometry().center());
        Q_EMIT qobject->central_output_changed(output, central_output);
    }

    NETWinInfo* info{nullptr};
    Wrapland::Server::Surface* surface{nullptr};
    quint32 surface_id{0};

    int bit_depth{24};
    QMargins client_frame_extents;

    // A UUID to uniquely identify this Toplevel independent of windowing system.
    QUuid internal_id;
    base::x11::xcb::window xcb_window{};

    // TODO: These are Unmanaged-only properties.
    bool is_outline{false};
    bool has_scheduled_release{false};
    xcb_visualid_t xcb_visual{XCB_NONE};
    // End of X11-only properties.

    bool has_in_content_deco{false};

    QRect m_frameGeometry;
    win::layer m_layer{win::layer::unknown};
    bool m_skipCloseAnimation{false};
    QVector<win::virtual_desktop*> m_desktops;

    /// Being used internally when emitting signals. Access via the space windows_map.
    uint32_t signal_id;

    explicit Toplevel(Space& space)
        : type(new win::transient<type>(this), space)
    {
    }

    Toplevel(win::remnant remnant, Space& space)
        : type(space)
    {
        this->remnant = std::move(remnant);
    }

    Toplevel(win::transient<type>* transient, Space& space)
        : space{space}
        , internal_id{QUuid::createUuid()}
        , signal_id{++space.window_id}
        , m_damageReplyPending(false)
    {
        space.windows_map.insert({signal_id, this});
        m_transient.reset(transient);
    }

    virtual void debug(QDebug& stream) const
    {
        if (remnant) {
            stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
        } else {
            stream << "\'ID:" << reinterpret_cast<void const*>(this) << xcb_window << "\'";
        }
    }

    void setDepth(int depth)
    {
        if (bit_depth == depth) {
            return;
        }
        const bool oldAlpha = hasAlpha();
        bit_depth = depth;
        if (oldAlpha != hasAlpha()) {
            Q_EMIT qobject->hasAlphaChanged();
        }
    }

private:
    void add_repaint_outputs(QRegion const& region)
    {
        if (kwinApp()->operationMode() == Application::OperationModeX11) {
            // On X11 we do not paint per output.
            return;
        }
        for (auto& out : space.base.outputs) {
            if (contains(repaint_outputs, out)) {
                continue;
            }
            if (region.intersected(out->geometry()).isEmpty()) {
                continue;
            }
            repaint_outputs.push_back(out);
        }
    }

    mutable bool m_render_shape_valid{false};
    mutable QRegion m_render_shape;

    bool m_damageReplyPending;
    xcb_xfixes_fetch_region_cookie_t m_regionCookie;

    std::unique_ptr<win::transient<type>> m_transient;

public:
    std::unique_ptr<win::control<type>> control;
    std::optional<win::remnant> remnant;

    win::transient<type>* transient() const
    {
        return m_transient.get();
    }

    /**
     * Below only for clients with control.
     * TODO: move this functionality into control.
     */

    virtual bool isCloseable() const = 0;
    virtual bool isShown() const = 0;
    virtual bool isHiddenInternal() const = 0;

    // TODO: remove boolean traps
    virtual void hideClient(bool hide) = 0;
    virtual void setFullScreen(bool set, bool user = true) = 0;
    virtual void handle_update_fullscreen(bool full) = 0;

    virtual win::maximize_mode maximizeMode() const
    {
        return win::maximize_mode::restore;
    }

    virtual bool noBorder() const = 0;
    virtual void setNoBorder(bool set) = 0;
    virtual void handle_update_no_border() = 0;

    /**
     * Returns whether the window is resizable or has a fixed size.
     */
    virtual bool isResizable() const = 0;
    /**
     * Returns whether the window is moveable or has a fixed position.
     */
    virtual bool isMovable() const = 0;
    /**
     * Returns whether the window can be moved to another screen.
     */
    virtual bool isMovableAcrossScreens() const = 0;

    virtual void handle_activated()
    {
    }

    virtual void takeFocus() = 0;
    virtual bool wantsInput() const
    {
        return false;
    }

    /**
     * Whether a dock window wants input.
     *
     * By default KWin doesn't pass focus to a dock window unless a force activate
     * request is provided.
     *
     * This method allows to have dock windows take focus also through flags set on
     * the window.
     *
     * The default implementation returns @c false.
     */
    virtual bool dockWantsInput() const
    {
        return false;
    }

    /**
     * Returns whether the window is maximizable or not.
     */
    virtual bool isMaximizable() const = 0;
    virtual bool isMinimizable() const = 0;
    virtual bool userCanSetFullScreen() const = 0;
    virtual bool userCanSetNoBorder() const = 0;
    virtual void checkNoBorder()
    {
        setNoBorder(false);
    }

    virtual xcb_timestamp_t userTime() const
    {
        return XCB_TIME_CURRENT_TIME;
    }

    virtual void updateWindowRules(win::rules::type selection)
    {
        if (space.rule_book->areUpdatesDisabled()) {
            return;
        }
        win::rules::update_window(control->rules, *this, static_cast<int>(selection));
    }

    virtual QSize minSize() const
    {
        return control->rules.checkMinSize(QSize(0, 0));
    }

    virtual QSize maxSize() const
    {
        return control->rules.checkMaxSize(QSize(INT_MAX, INT_MAX));
    }

    virtual void setFrameGeometry(QRect const& rect) = 0;
    virtual void apply_restore_geometry(QRect const& restore_geo) = 0;
    virtual void restore_geometry_from_fullscreen() = 0;

    virtual bool hasStrut() const = 0;

    // TODO: fix boolean traps
    virtual void updateDecoration(bool check_workspace_pos, bool force = false) = 0;
    virtual void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
    {
        if (remnant) {
            return remnant->data.layout_decoration_rects(left, top, right, bottom);
        }
        win::layout_decoration_rects(this, left, top, right, bottom);
    }

    /**
     * Returns whether the window provides context help or not. If it does,
     * you should show a help menu item or a help button like '?' and call
     * contextHelp() if this is invoked.
     *
     * Default implementation returns @c false.
     * @see showContextHelp;
     */
    virtual bool providesContextHelp() const
    {
        return false;
    }

    /**
     * Invokes context help on the window. Only works if the window
     * actually provides context help.
     *
     * Default implementation does nothing.
     *
     * @see providesContextHelp()
     */
    virtual void showContextHelp()
    {
    }

    /**
     * Restores the AbstractClient after it had been hidden due to show on screen edge
     * functionality. The AbstractClient also gets raised (e.g. Panel mode windows can cover)
     * and the AbstractClient gets informed in a window specific way that it is shown and raised
     * again.
     */
    virtual void showOnScreenEdge()
    {
    }

    /**
     * Tries to terminate the process of this AbstractClient.
     *
     * Implementing subclasses can perform a windowing system solution for terminating.
     */
    virtual void killWindow()
    {
    }

    virtual bool isInitialPositionSet() const
    {
        return false;
    }

    /**
     * Default implementation returns @c null.
     * Mostly intended for X11 clients, from EWMH:
     * @verbatim
     * If the WM_TRANSIENT_FOR property is set to None or Root window, the window should be
     * treated as a transient for all other windows in the same group. It has been noted that
     * this is a slight ICCCM violation, but as this behavior is pretty standard for many
     * toolkits and window managers, and is extremely unlikely to break anything, it seems
     * reasonable to document it as standard.
     * @endverbatim
     */
    virtual bool groupTransient() const
    {
        return false;
    }

    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual win::x11::group<space_t> const* group() const
    {
        return nullptr;
    }

    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual win::x11::group<space_t>* group()
    {
        return nullptr;
    }

    virtual bool supportsWindowRules() const
    {
        return control != nullptr;
    }

    virtual QSize basicUnit() const
    {
        return QSize(1, 1);
    }

    virtual void setBlockingCompositing(bool /*block*/)
    {
    }

    virtual bool isBlockingCompositing()
    {
        return false;
    }

    /**
     * Called from win::start_move_resize.
     *
     * Implementing classes should return @c false if starting move resize should
     * get aborted. In that case win::start_move_resize will also return @c false.
     *
     * Base implementation returns @c true.
     */
    virtual bool doStartMoveResize()
    {
        return true;
    }

    /**
     * Called from win::perform_move_resize() after actually performing the change of geometry.
     * Implementing subclasses can perform windowing system specific handling here.
     *
     * Default implementation does nothing.
     */
    virtual void doPerformMoveResize()
    {
    }

    /**
     * Leaves the move resize mode.
     *
     * Inheriting classes must invoke the base implementation which
     * ensures that the internal mode is properly ended.
     */
    virtual void leaveMoveResize()
    {
        win::set_move_resize_window(space, nullptr);
        control->move_resize.enabled = false;
        if (space.edges->desktop_switching.when_moving_client) {
            space.edges->reserveDesktopSwitching(false, Qt::Vertical | Qt::Horizontal);
        }
        if (control->electric_maximizing) {
            space.outline->hide();
            win::elevate(this, false);
        }
    }

    /**
     * Called during handling a resize. Implementing subclasses can use this
     * method to perform windowing system specific syncing.
     *
     * Default implementation does nothing.
     */
    virtual void doResizeSync()
    {
    }

    /**
     * Whether a sync request is still pending.
     * Default implementation returns @c false.
     */
    virtual bool isWaitingForMoveResizeSync() const
    {
        return false;
    }

    /**
     * Called from win::set_active once the active value got updated, but before the changed
     * signal is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetActive()
    {
    }

    /**
     * Called from setKeepAbove once the keepBelow value got updated, but before the changed
     * signal is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepAbove()
    {
    }

    /**
     * Called from setKeepBelow once the keepBelow value got updated, but before the changed
     * signal is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepBelow()
    {
    }

    /**
     * Called from @ref minimize and @ref unminimize once the minimized value got updated, but
     * before the changed signal is emitted.
     *
     * Default implementation does nothig.
     */
    virtual void doMinimize()
    {
    }

    /**
     * Called from set_desktops once the desktop value got updated, but before the changed
     * signal is emitted.
     *
     * Default implementation does nothing.
     * @param desktop The new desktop the Client is on
     * @param was_desk The desktop the Client was on before
     */
    virtual void doSetDesktop(int /*desktop*/, int /*was_desk*/)
    {
    }

    virtual QSize resizeIncrements() const
    {
        return QSize(1, 1);
    }

    virtual void updateColorScheme()
    {
    }

    virtual void updateCaption()
    {
    }

    /**
     * Whether the window accepts focus.
     * The difference to wantsInput is that the implementation should not check rules and return
     * what the window effectively supports.
     */
    virtual bool acceptsFocus() const = 0;

    virtual void update_maximized(win::maximize_mode /*mode*/)
    {
    }

    virtual void closeWindow() = 0;

    virtual bool performMouseCommand(base::options_qobject::MouseCommand cmd,
                                     const QPoint& globalPos)
    {
        return win::perform_mouse_command(*this, cmd, globalPos);
    }

    virtual type* findModal()
    {
        return nullptr;
    }

    virtual bool belongsToSameApplication(type const* /*other*/,
                                          win::same_client_check /*checks*/) const
    {
        return false;
    }

    virtual QRect iconGeometry() const
    {
        return space.get_icon_geometry(this);
    }

    virtual void setShortcutInternal()
    {
        updateCaption();
        win::window_shortcut_updated(space, this);
    }

    // Applies Force, ForceTemporarily and ApplyNow rules
    // Used e.g. after the rules have been modified using the kcm.
    virtual void applyWindowRules()
    {
        // apply force rules
        // Placement - does need explicit update, just like some others below
        // Geometry : setGeometry() doesn't check rules
        auto client_rules = control->rules;

        auto const orig_geom = frameGeometry();
        auto const geom = client_rules.checkGeometry(orig_geom);

        if (geom != orig_geom) {
            setFrameGeometry(geom);
        }

        // MinSize, MaxSize handled by Geometry
        // IgnoreGeometry
        win::set_desktop(this, desktop());

        // TODO(romangg): can central_output be null?
        win::send_to_screen(space, this, *central_output);
        // Type
        win::maximize(this, maximizeMode());

        // Minimize : functions don't check
        win::set_minimized(this, client_rules.checkMinimize(control->minimized));

        win::set_original_skip_taskbar(this, control->skip_taskbar());
        win::set_skip_pager(this, control->skip_pager());
        win::set_skip_switcher(this, control->skip_switcher());
        win::set_keep_above(this, control->keep_above);
        win::set_keep_below(this, control->keep_below);
        setFullScreen(control->fullscreen, true);
        setNoBorder(noBorder());
        updateColorScheme();

        // FSP
        // AcceptFocus :
        if (win::most_recently_activated_window(space) == this
            && !client_rules.checkAcceptFocus(true)) {
            win::activate_next_window(space, this);
        }

        // Closeable
        if (auto s = size(); s != size() && s.isValid()) {
            win::constrained_resize(this, s);
        }

        // Autogrouping : Only checked on window manage
        // AutogroupInForeground : Only checked on window manage
        // AutogroupById : Only checked on window manage
        // StrictGeometry
        win::set_shortcut(this, control->rules.checkShortcut(control->shortcut.toString()));

        // see also X11Client::setActive()
        if (control->active) {
            setOpacity(control->rules.checkOpacityActive(qRound(opacity() * 100.0)) / 100.0);
            win::set_global_shortcuts_disabled(space,
                                               control->rules.checkDisableGlobalShortcuts(false));
        } else {
            setOpacity(control->rules.checkOpacityInactive(qRound(opacity() * 100.0)) / 100.0);
        }

        win::set_desktop_file_name(
            this, control->rules.checkDesktopFile(control->desktop_file_name).toUtf8());
    }
};

template<typename Space>
QDebug& operator<<(QDebug& stream, Toplevel<Space> const* win)
{
    if (!win) {
        return stream << "\'NULL\'";
    }
    win->debug(stream);
    return stream;
}

}
