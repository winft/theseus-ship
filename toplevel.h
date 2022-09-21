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
#include "win/damage.h"
#include "win/remnant.h"
#include "win/rules.h"
#include "win/rules/ruling.h"
#include "win/rules/update.h"
#include "win/shortcut_set.h"
#include "win/virtual_desktops.h"
#include "win/window_geometry.h"
#include "win/window_qobject.h"

#include <NETWM>
#include <QMatrix4x4>
#include <QUuid>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>

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

    win::window_geometry geo;

    std::unique_ptr<render_t> render;

    struct {
        QString normal;
        // suffix added to normal caption (e.g. shortcut, machine name, etc.).
        QString suffix;
    } caption;

    struct {
        // Always lowercase
        QByteArray res_name;
        QByteArray res_class;
    } wm_class;

    struct {
        QMetaObject::Connection frame_update_outputs;
        QMetaObject::Connection screens_update_outputs;
        QMetaObject::Connection check_screen;
    } notifiers;

    // Relative to client geometry.
    QRegion damage_region;

    // Relative to frame geometry.
    QRegion repaints_region;
    QRegion layer_repaints_region;
    bool ready_for_painting{false};
    bool is_damaged{false};
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

    virtual QRegion render_region() const
    {
        if (remnant) {
            return remnant->data.render_region;
        }

        auto const render_geo = win::render_geometry(this);
        return QRegion(0, 0, render_geo.width(), render_geo.height());
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
        return desktops.isEmpty() ? static_cast<int>(NET::OnAllDesktops)
                                  : desktops.last()->x11DesktopNumber();
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

    virtual double opacity() const = 0;
    virtual void setOpacity(double new_opacity) = 0;

    virtual void setupCompositing() = 0;

    virtual void add_scene_window_addon()
    {
    }

    virtual void finishCompositing()
    {
        win::finish_compositing(*this);
    }

    virtual bool has_pending_repaints() const
    {
        return !win::repaints(*this).isEmpty();
    }

    /**
     * Whether the Toplevel currently wants the shadow to be rendered. Default
     * implementation always returns @c true.
     */
    virtual bool wantsShadowToBeRendered() const
    {
        return true;
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
        return win::layer_for_dock(*this);
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

    NETWinInfo* info{nullptr};
    Wrapland::Server::Surface* surface{nullptr};
    quint32 surface_id{0};

    int bit_depth{24};

    // A UUID to uniquely identify this Toplevel independent of windowing system.
    QUuid internal_id;
    base::x11::xcb::window xcb_window{};

    bool is_outline{false};

    mutable bool is_render_shape_valid{false};

    win::layer layer{win::layer::unknown};
    bool skip_close_animation{false};
    QVector<win::virtual_desktop*> desktops;

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
    {
        space.windows_map.insert({signal_id, this});
        this->transient.reset(transient);
    }

    virtual void debug(QDebug& stream) const
    {
        if (remnant) {
            stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
        } else {
            stream << "\'ID:" << reinterpret_cast<void const*>(this) << xcb_window << "\'";
        }
    }

    std::unique_ptr<win::transient<type>> transient;

public:
    std::unique_ptr<win::control<type>> control;
    std::optional<win::remnant> remnant;

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

    virtual void leaveMoveResize()
    {
        win::leave_move_resize(*this);
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

    virtual void applyWindowRules()
    {
        win::apply_window_rules(*this);
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
