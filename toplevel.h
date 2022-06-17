/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

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

#ifndef KWIN_TOPLEVEL_H
#define KWIN_TOPLEVEL_H

#include "base/output.h"
#include "base/x11/xcb/window.h"
#include "input/cursor.h"
#include "rules/rules.h"
#include "win/virtual_desktops.h"

#include <NETWM>
// Qt
#include <QMatrix4x4>
#include <QObject>
#include <QUuid>
// xcb
#include <xcb/damage.h>
#include <xcb/xfixes.h>
// c++
#include <functional>
#include <memory>

class QOpenGLFramebufferObject;

namespace Wrapland::Server
{
class Surface;
}

namespace KWin
{

namespace render
{
class effects_window_impl;
class window;
}

namespace win
{
namespace x11
{
class client_machine;
class group;
}

class control;
class remnant;
class space;
class transient;
}

class KWIN_EXPORT Toplevel : public QObject
{
    Q_OBJECT

public:
    constexpr static bool is_toplevel{true};

    std::unique_ptr<render::window> render;

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

    base::output const* central_output{nullptr};

    /**
     * Records all outputs that still need to be repainted for the current repaint regions.
     */
    std::vector<base::output*> repaint_outputs;
    win::space& space;

    explicit Toplevel(win::space& space);
    ~Toplevel() override;

    virtual xcb_window_t frameId() const;

    QRegion render_region() const;
    void discard_shape();
    void discard_quads();

    /**
     * Returns the geometry of the Toplevel, excluding invisible portions, e.g.
     * server-side and client-side drop shadows, etc.
     */
    QRect frameGeometry() const;
    void set_frame_geometry(QRect const& rect);

    QSize size() const;
    QPoint pos() const;

    /**
     * Returns the ratio between physical pixels and device-independent pixels for
     * the attached buffer (or pixmap).
     *
     * For X11 clients, this method always returns 1.
     */
    virtual qreal bufferScale() const;

    virtual bool is_wayland_window() const;
    virtual bool isClient() const;

    // prefer isXXX() instead
    // 0 for supported types means default for managed/unmanaged types
    virtual NET::WindowType windowType(bool direct = false, int supported_types = 0) const;

    virtual bool isLockScreen() const;
    virtual bool isInputMethod() const;
    virtual bool isOutline() const;

    /**
     * Returns the virtual desktop within the workspace() the client window
     * is located in, 0 if it isn't located on any special desktop (not mapped yet),
     * or NET::OnAllDesktops. Do not use desktop() directly, use
     * isOnDesktop() instead.
     */
    virtual int desktop() const;
    QVector<win::virtual_desktop*> desktops() const;
    void set_desktops(QVector<win::virtual_desktop*> const& desktops);

    bool isOnDesktop(int d) const;
    bool isOnCurrentDesktop() const;
    bool isOnAllDesktops() const;

    virtual QByteArray windowRole() const;
    QByteArray wmClientMachine(bool use_localhost) const;
    virtual bool isLocalhost() const;
    xcb_window_t wmClientLeader() const;
    virtual pid_t pid() const;

    virtual void setOpacity(double opacity);
    virtual double opacity() const;
    bool hasAlpha() const;
    virtual bool setupCompositing();
    virtual void add_scene_window_addon();
    virtual void finishCompositing();

    Q_INVOKABLE void addRepaint(QRegion const& region);
    Q_INVOKABLE void addLayerRepaint(QRegion const& r);

    Q_INVOKABLE virtual void addRepaintFull();

    virtual bool has_pending_repaints() const;
    QRegion repaints() const;
    void resetRepaints(base::output* output);

    void resetDamage();
    void addDamageFull();
    virtual void addDamage(const QRegion& damage);

    static Toplevel* create_remnant(Toplevel* source);

    /**
     * Whether the Toplevel currently wants the shadow to be rendered. Default
     * implementation always returns @c true.
     */
    virtual bool wantsShadowToBeRendered() const;

    win::layer layer() const;
    void set_layer(win::layer layer);

    /**
     * Resets the damage state and sends a request for the damage region.
     * A call to this function must be followed by a call to getDamageRegionReply(),
     * or the reply will be leaked.
     *
     * Returns true if the window was damaged, and false otherwise.
     */
    bool resetAndFetchDamage();

    /**
     * Gets the reply from a previous call to resetAndFetchDamage().
     * Calling this function is a no-op if there is no pending reply.
     * Call damage() to return the fetched region.
     */
    void getDamageRegionReply();

    bool skipsCloseAnimation() const;
    void setSkipCloseAnimation(bool set);

    /**
     * Maps from global to window coordinates.
     */
    QMatrix4x4 input_transform() const;

    /**
     * Can be implemented by child classes to add additional checks to the ones in win::is_popup.
     */
    virtual bool is_popup_end() const;

    virtual win::layer layer_for_dock() const;

    /**
     * Returns whether this is an internal client.
     *
     * Internal clients are created by KWin and used for special purpose windows,
     * like the task switcher, etc.
     *
     * Default implementation returns @c false.
     */
    virtual bool isInternal() const;
    virtual bool belongsToDesktop() const;
    virtual void checkTransient(Toplevel* window);

    void setWindowHandles(xcb_window_t w);
    void disownDataPassedToDeleted();

    virtual void damageNotifyEvent();
    void discard_buffer();

    void setResourceClass(const QByteArray& name, const QByteArray& className = QByteArray());

    NETWinInfo* info{nullptr};
    Wrapland::Server::Surface* surface{nullptr};
    quint32 surface_id{0};

    // TODO: These are X11-only properties, should go into a separate struct once we use class
    //       templates only.
    int supported_default_types{0};
    int bit_depth{24};
    QMargins client_frame_extents;
    win::x11::client_machine* client_machine{nullptr};

    // A UUID to uniquely identify this Toplevel independent of windowing system.
    QUuid internal_id;
    base::x11::xcb::window xcb_window{};

    // TODO: These are Unmanaged-only properties.
    bool is_outline{false};
    bool has_scheduled_release{false};
    xcb_visualid_t xcb_visual{XCB_NONE};
    // End of X11-only properties.

    bool has_in_content_deco{false};

Q_SIGNALS:
    void opacityChanged(KWin::Toplevel* toplevel, qreal oldOpacity);
    void damaged(KWin::Toplevel* toplevel, QRegion const& damage);

    void frame_geometry_changed(KWin::Toplevel* toplevel, QRect const& old);
    void visible_geometry_changed();

    void paddingChanged(KWin::Toplevel* toplevel, const QRect& old);
    void remnant_created(KWin::Toplevel* remnant);
    void closed(KWin::Toplevel* toplevel);
    void windowShown(KWin::Toplevel* toplevel);
    void windowHidden(KWin::Toplevel* toplevel);
    /**
     * Signal emitted when the window's shape state changed. That is if it did not have a shape
     * and received one or if the shape was withdrawn. Think of Chromium enabling/disabling KWin's
     * decoration.
     */
    void shapedChanged();
    /**
     * Emitted whenever the state changes in a way, that the Compositor should
     * schedule a repaint of the scene.
     */
    void needsRepaint();
    /**
     * Emitted whenever the Toplevel's output changes. This can happen either in consequence to
     * an output being removed/added or if the Toplevel's geometry changes.
     */
    void central_output_changed(base::output const* old_out, base::output const* new_out);
    void skipCloseAnimationChanged();
    /**
     * Emitted whenever the window role of the window changes.
     * @since 5.0
     */
    void windowRoleChanged();
    /**
     * Emitted whenever the window class name or resource name of the window changes.
     * @since 5.0
     */
    void windowClassChanged();
    /**
     * Emitted when a Wayland Surface gets associated with this Toplevel.
     * @since 5.3
     */
    void surfaceIdChanged(quint32);
    /**
     * @since 5.4
     */
    void hasAlphaChanged();

    /**
     * Emitted whenever the Surface for this Toplevel changes.
     */
    void surfaceChanged();

    /**
     * Emitted whenever the client's shadow changes.
     * @since 5.15
     */
    void shadowChanged();

    /**
     * Below signals only relevant for toplevels with control.
     */
    void iconChanged();
    void unresponsiveChanged(bool);
    void captionChanged();
    void hasApplicationMenuChanged(bool);
    void applicationMenuChanged();
    void applicationMenuActiveChanged(bool);

public Q_SLOTS:
    /**
     * Checks whether the screen number for this Toplevel changed and updates if needed.
     * Any method changing the geometry of the Toplevel should call this method.
     */
    void checkScreen();
    void setupCheckScreenConnection();
    void removeCheckScreenConnection();

    void setReadyForPainting();

protected:
    Toplevel(win::transient* transient, win::space& space);

    virtual void debug(QDebug& stream) const;
    void copyToDeleted(Toplevel* c);
    friend QDebug& operator<<(QDebug& stream, const Toplevel*);
    void setDepth(int depth);

    xcb_window_t m_wmClientLeader{XCB_WINDOW_NONE};

private:
    void handle_output_added(base::output* output);
    void handle_output_removed(base::output* output);
    void add_repaint_outputs(QRegion const& region);

    QRect m_frameGeometry;
    win::layer m_layer{win::layer::unknown};
    mutable bool m_render_shape_valid{false};
    mutable QRegion m_render_shape;

    bool m_damageReplyPending;
    xcb_xfixes_fetch_region_cookie_t m_regionCookie;
    bool m_skipCloseAnimation;
    QVector<win::virtual_desktop*> m_desktops;

    std::unique_ptr<win::transient> m_transient;

public:
    std::unique_ptr<win::control> control;
    std::unique_ptr<win::remnant> remnant;

    win::transient* transient() const;

    /**
     * Below only for clients with control.
     * TODO: move this functionality into control.
     */

    virtual bool isCloseable() const;
    // TODO: remove boolean trap
    virtual bool isShown() const;
    virtual bool isHiddenInternal() const;
    // TODO: remove boolean trap
    virtual void hideClient(bool hide);

    virtual void setFullScreen(bool set, bool user = true);

    virtual win::maximize_mode maximizeMode() const;

    virtual bool noBorder() const;
    virtual void setNoBorder(bool set);

    /**
     * Returns whether the window is resizable or has a fixed size.
     */
    virtual bool isResizable() const;
    /**
     * Returns whether the window is moveable or has a fixed position.
     */
    virtual bool isMovable() const;
    /**
     * Returns whether the window can be moved to another screen.
     */
    virtual bool isMovableAcrossScreens() const;

    virtual void takeFocus();
    virtual bool wantsInput() const;

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
    virtual bool dockWantsInput() const;

    /**
     * Returns whether the window is maximizable or not.
     */
    virtual bool isMaximizable() const;
    virtual bool isMinimizable() const;
    virtual bool userCanSetFullScreen() const;
    virtual bool userCanSetNoBorder() const;
    virtual void checkNoBorder();

    virtual xcb_timestamp_t userTime() const;
    virtual void updateWindowRules(Rules::Types selection);

    virtual QSize minSize() const;
    virtual QSize maxSize() const;

    virtual void setFrameGeometry(QRect const& rect);

    virtual bool hasStrut() const;

    // TODO: fix boolean traps
    virtual void updateDecoration(bool check_workspace_pos, bool force = false);
    virtual void layoutDecorationRects(QRect& left, QRect& top, QRect& right, QRect& bottom) const;

    /**
     * Returns whether the window provides context help or not. If it does,
     * you should show a help menu item or a help button like '?' and call
     * contextHelp() if this is invoked.
     *
     * Default implementation returns @c false.
     * @see showContextHelp;
     */
    virtual bool providesContextHelp() const;

    /**
     * Invokes context help on the window. Only works if the window
     * actually provides context help.
     *
     * Default implementation does nothing.
     *
     * @see providesContextHelp()
     */
    virtual void showContextHelp();

    /**
     * Restores the AbstractClient after it had been hidden due to show on screen edge
     * functionality. The AbstractClient also gets raised (e.g. Panel mode windows can cover) and
     * the AbstractClient gets informed in a window specific way that it is shown and raised again.
     */
    virtual void showOnScreenEdge();

    /**
     * Tries to terminate the process of this AbstractClient.
     *
     * Implementing subclasses can perform a windowing system solution for terminating.
     */
    virtual void killWindow();

    virtual bool isInitialPositionSet() const;

    /**
     * Default implementation returns @c null.
     * Mostly intended for X11 clients, from EWMH:
     * @verbatim
     * If the WM_TRANSIENT_FOR property is set to None or Root window, the window should be
     * treated as a transient for all other windows in the same group. It has been noted that this
     * is a slight ICCCM violation, but as this behavior is pretty standard for many toolkits and
     * window managers, and is extremely unlikely to break anything, it seems reasonable to document
     * it as standard.
     * @endverbatim
     */
    virtual bool groupTransient() const;
    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual win::x11::group const* group() const;
    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual win::x11::group* group();

    virtual bool supportsWindowRules() const;

    virtual QSize basicUnit() const;

    virtual void setBlockingCompositing(bool block);
    virtual bool isBlockingCompositing();

    /**
     * Called from win::start_move_resize.
     *
     * Implementing classes should return @c false if starting move resize should
     * get aborted. In that case win::start_move_resize will also return @c false.
     *
     * Base implementation returns @c true.
     */
    virtual bool doStartMoveResize();

    /**
     * Called from win::perform_move_resize() after actually performing the change of geometry.
     * Implementing subclasses can perform windowing system specific handling here.
     *
     * Default implementation does nothing.
     */
    virtual void doPerformMoveResize();

    /**
     * Leaves the move resize mode.
     *
     * Inheriting classes must invoke the base implementation which
     * ensures that the internal mode is properly ended.
     */
    virtual void leaveMoveResize();

    /**
     * Called during handling a resize. Implementing subclasses can use this
     * method to perform windowing system specific syncing.
     *
     * Default implementation does nothing.
     */
    virtual void doResizeSync();

    /**
     * Whether a sync request is still pending.
     * Default implementation returns @c false.
     */
    virtual bool isWaitingForMoveResizeSync() const;

    /**
     * Called from win::set_active once the active value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetActive();

    /**
     * Called from setKeepAbove once the keepBelow value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepAbove();
    /**
     * Called from setKeepBelow once the keepBelow value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepBelow();

    /**
     * Called from @ref minimize and @ref unminimize once the minimized value got updated, but
     * before the changed signal is emitted.
     *
     * Default implementation does nothig.
     */
    virtual void doMinimize();

    /**
     * Called from set_desktops once the desktop value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     * @param desktop The new desktop the Client is on
     * @param was_desk The desktop the Client was on before
     */
    virtual void doSetDesktop(int desktop, int was_desk);

    virtual QSize resizeIncrements() const;

    virtual void updateColorScheme();
    virtual void updateCaption();

    /**
     * Whether the window accepts focus.
     * The difference to wantsInput is that the implementation should not check rules and return
     * what the window effectively supports.
     */
    virtual bool acceptsFocus() const;

    virtual void update_maximized(win::maximize_mode mode);

    Q_INVOKABLE virtual void closeWindow();

    virtual bool performMouseCommand(base::options::MouseCommand, const QPoint& globalPos);

    virtual Toplevel* findModal();

    virtual bool belongsToSameApplication(Toplevel const* other,
                                          win::same_client_check checks) const;

    virtual QRect iconGeometry() const;
    virtual void setShortcutInternal();
    virtual void applyWindowRules();

Q_SIGNALS:
    void activeChanged();
    void demandsAttentionChanged();

    // to be forwarded by Workspace
    void desktopPresenceChanged(KWin::Toplevel* window, int);
    void desktopChanged();
    void x11DesktopIdsChanged();

    void minimizedChanged();
    void clientMinimized(KWin::Toplevel* window, bool animate);
    void clientUnminimized(KWin::Toplevel* window, bool animate);
    void clientMaximizedStateChanged(KWin::Toplevel* window, KWin::win::maximize_mode);
    void clientMaximizedStateChanged(KWin::Toplevel* window, bool h, bool v);
    void quicktiling_changed();
    void keepAboveChanged(bool);
    void keepBelowChanged(bool);
    void blockingCompositingChanged(KWin::Toplevel* window);

    void fullScreenChanged();
    void skipTaskbarChanged();
    void skipPagerChanged();
    void skipSwitcherChanged();

    void paletteChanged(const QPalette& p);
    void colorSchemeChanged();
    void transientChanged();
    void modalChanged();
    void moveResizedChanged();
    void moveResizeCursorChanged(input::cursor_shape);
    void clientStartUserMovedResized(KWin::Toplevel* window);
    void clientStepUserMovedResized(KWin::Toplevel* window, const QRect&);
    void clientFinishUserMovedResized(KWin::Toplevel* window);

    void closeableChanged(bool);
    void minimizeableChanged(bool);
    void maximizeableChanged(bool);
    void desktopFileNameChanged();
};

inline QRect Toplevel::frameGeometry() const
{
    return m_frameGeometry;
}

inline QSize Toplevel::size() const
{
    return m_frameGeometry.size();
}

inline QPoint Toplevel::pos() const
{
    return m_frameGeometry.topLeft();
}

inline bool Toplevel::isLockScreen() const
{
    return false;
}

inline bool Toplevel::isInputMethod() const
{
    return false;
}

inline bool Toplevel::hasAlpha() const
{
    return bit_depth == 32;
}

KWIN_EXPORT QDebug& operator<<(QDebug& stream, const Toplevel*);

} // namespace
Q_DECLARE_METATYPE(KWin::Toplevel*)

#endif
