/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwineffects/effect_screen.h"
#include <kwinconfig.h>
#include <kwineffects/export.h>
#include <kwineffects/types.h>
#include <kwinglobals.h>

#include <QIcon>
#include <QObject>

namespace KDecoration2
{
class Decoration;
}

namespace Wrapland::Server
{
class Surface;
}

namespace KWin
{

class WindowQuadList;

class EffectWindowGroup
{
public:
    virtual ~EffectWindowGroup()
    {
    }

    virtual EffectWindowList members() const = 0;
};

/**
 * @short Representation of a window used by/for Effect classes.
 *
 * The purpose is to hide internal data and also to serve as a single
 *  representation for the case when Client/Unmanaged becomes Deleted.
 */
class KWINEFFECTS_EXPORT EffectWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRect geometry READ frameGeometry)
    Q_PROPERTY(QRect expandedGeometry READ expandedGeometry)
    Q_PROPERTY(int height READ height)
    Q_PROPERTY(qreal opacity READ opacity)
    Q_PROPERTY(QPoint pos READ pos)
    Q_PROPERTY(KWin::EffectScreen* screen READ screen)
    Q_PROPERTY(QSize size READ size)
    Q_PROPERTY(int width READ width)
    Q_PROPERTY(int x READ x)
    Q_PROPERTY(int y READ y)
    Q_PROPERTY(QVector<uint> desktops READ desktops)
    Q_PROPERTY(bool onAllDesktops READ isOnAllDesktops)
    Q_PROPERTY(bool onCurrentDesktop READ isOnCurrentDesktop)
    Q_PROPERTY(QRect rect READ rect)
    Q_PROPERTY(QString windowClass READ windowClass)
    Q_PROPERTY(QString windowRole READ windowRole)
    /**
     * Returns whether the window is a desktop background window (the one with wallpaper).
     * See _NET_WM_WINDOW_TYPE_DESKTOP at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool desktopWindow READ isDesktop)
    /**
     * Returns whether the window is a dock (i.e. a panel).
     * See _NET_WM_WINDOW_TYPE_DOCK at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html
     * .
     */
    Q_PROPERTY(bool dock READ isDock)
    /**
     * Returns whether the window is a standalone (detached) toolbar window.
     * See _NET_WM_WINDOW_TYPE_TOOLBAR at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool toolbar READ isToolbar)
    /**
     * Returns whether the window is a torn-off menu.
     * See _NET_WM_WINDOW_TYPE_MENU at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html
     * .
     */
    Q_PROPERTY(bool menu READ isMenu)
    /**
     * Returns whether the window is a "normal" window, i.e. an application or any other window
     * for which none of the specialized window types fit.
     * See _NET_WM_WINDOW_TYPE_NORMAL at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool normalWindow READ isNormalWindow)
    /**
     * Returns whether the window is a dialog window.
     * See _NET_WM_WINDOW_TYPE_DIALOG at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool dialog READ isDialog)
    /**
     * Returns whether the window is a splashscreen. Note that many (especially older) applications
     * do not support marking their splash windows with this type.
     * See _NET_WM_WINDOW_TYPE_SPLASH at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool splash READ isSplash)
    /**
     * Returns whether the window is a utility window, such as a tool window.
     * See _NET_WM_WINDOW_TYPE_UTILITY at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool utility READ isUtility)
    /**
     * Returns whether the window is a dropdown menu (i.e. a popup directly or indirectly open
     * from the applications menubar).
     * See _NET_WM_WINDOW_TYPE_DROPDOWN_MENU at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool dropdownMenu READ isDropdownMenu)
    /**
     * Returns whether the window is a popup menu (that is not a torn-off or dropdown menu).
     * See _NET_WM_WINDOW_TYPE_POPUP_MENU at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool popupMenu READ isPopupMenu)
    /**
     * Returns whether the window is a tooltip.
     * See _NET_WM_WINDOW_TYPE_TOOLTIP at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool tooltip READ isTooltip)
    /**
     * Returns whether the window is a window with a notification.
     * See _NET_WM_WINDOW_TYPE_NOTIFICATION at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool notification READ isNotification)
    /**
     * Returns whether the window is a window with a critical notification.
     * using the non-standard _KDE_NET_WM_WINDOW_TYPE_CRITICAL_NOTIFICATION
     */
    Q_PROPERTY(bool criticalNotification READ isCriticalNotification)
    /**
     * Returns whether the window is an on screen display window
     * using the non-standard _KDE_NET_WM_WINDOW_TYPE_ON_SCREEN_DISPLAY
     */
    Q_PROPERTY(bool onScreenDisplay READ isOnScreenDisplay)
    /**
     * Returns whether the window is a combobox popup.
     * See _NET_WM_WINDOW_TYPE_COMBO at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(bool comboBox READ isComboBox)
    /**
     * Returns whether the window is a Drag&Drop icon.
     * See _NET_WM_WINDOW_TYPE_DND at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html
     * .
     */
    Q_PROPERTY(bool dndIcon READ isDNDIcon)
    /**
     * Whether this EffectWindow is managed by KWin (it has control over its placement and other
     * aspects, as opposed to override-redirect windows that are entirely handled by the
     * application).
     */
    Q_PROPERTY(bool managed READ isManaged)
    /**
     * Whether this EffectWindow represents an already deleted window and only kept for the
     * compositor for animations.
     */
    Q_PROPERTY(bool deleted READ isDeleted)
    /**
     * The Caption of the window. Read from WM_NAME property together with a suffix for hostname and
     * shortcut.
     */
    Q_PROPERTY(QString caption READ caption)
    /**
     * Whether the window is set to be kept above other windows.
     */
    Q_PROPERTY(bool keepAbove READ keepAbove)
    /**
     * Whether the window is set to be kept below other windows.
     */
    Q_PROPERTY(bool keepBelow READ keepBelow)
    /**
     * Whether the window is minimized.
     */
    Q_PROPERTY(bool minimized READ isMinimized WRITE setMinimized)
    /**
     * Whether the window represents a modal window.
     */
    Q_PROPERTY(bool modal READ isModal)
    /**
     * Whether the window is moveable. Even if it is not moveable, it might be possible to move
     * it to another screen.
     * @see moveableAcrossScreens
     */
    Q_PROPERTY(bool moveable READ isMovable)
    /**
     * Whether the window can be moved to another screen.
     * @see moveable
     */
    Q_PROPERTY(bool moveableAcrossScreens READ isMovableAcrossScreens)
    /**
     * By how much the window wishes to grow/shrink at least. Usually QSize(1,1).
     * MAY BE DISOBEYED BY THE WM! It's only for information, do NOT rely on it at all.
     */
    Q_PROPERTY(QSize basicUnit READ basicUnit)
    /**
     * Whether the window is currently being moved by the user.
     */
    Q_PROPERTY(bool move READ isUserMove)
    /**
     * Whether the window is currently being resized by the user.
     */
    Q_PROPERTY(bool resize READ isUserResize)
    /**
     * The optional geometry representing the minimized Client in e.g a taskbar.
     * See _NET_WM_ICON_GEOMETRY at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(QRect iconGeometry READ iconGeometry)
    /**
     * Returns whether the window is any of special windows types (desktop, dock, splash, ...),
     * i.e. window types that usually don't have a window frame and the user does not use window
     * management (moving, raising,...) on them.
     */
    Q_PROPERTY(bool specialWindow READ isSpecialWindow)
    Q_PROPERTY(QIcon icon READ icon)
    /**
     * Whether the window should be excluded from window switching effects.
     */
    Q_PROPERTY(bool skipSwitcher READ isSkipSwitcher)
    /**
     * Geometry of the actual window contents inside the whole (including decorations) window.
     */
    Q_PROPERTY(QRect contentsRect READ contentsRect)
    /**
     * Geometry of the transparent rect in the decoration.
     * May be different from contentsRect if the decoration is extended into the client area.
     */
    Q_PROPERTY(QRect decorationInnerRect READ decorationInnerRect)
    Q_PROPERTY(bool hasDecoration READ hasDecoration)
    Q_PROPERTY(QStringList activities READ activities)
    Q_PROPERTY(bool onCurrentActivity READ isOnCurrentActivity)
    Q_PROPERTY(bool onAllActivities READ isOnAllActivities)
    /**
     * Whether the decoration currently uses an alpha channel.
     * @since 4.10
     */
    Q_PROPERTY(bool decorationHasAlpha READ decorationHasAlpha)
    /**
     * Whether the window is currently visible to the user, that is:
     * <ul>
     * <li>Not minimized</li>
     * <li>On current desktop</li>
     * <li>On current activity</li>
     * </ul>
     * @since 4.11
     */
    Q_PROPERTY(bool visible READ isVisible)
    /**
     * Whether the window does not want to be animated on window close.
     * In case this property is @c true it is not useful to start an animation on window close.
     * The window will not be visible, but the animation hooks are executed.
     * @since 5.0
     */
    Q_PROPERTY(bool skipsCloseAnimation READ skipsCloseAnimation)

    /**
     * Interface to the corresponding wayland surface.
     * relevant only in Wayland, on X11 it will be nullptr
     */
    Q_PROPERTY(Wrapland::Server::Surface* surface READ surface)

    /**
     * Whether the window is fullscreen.
     * @since 5.6
     */
    Q_PROPERTY(bool fullScreen READ isFullScreen)

    /**
     * Whether this client is unresponsive.
     *
     * When an application failed to react on a ping request in time, it is
     * considered unresponsive. This usually indicates that the application froze or crashed.
     *
     * @since 5.10
     */
    Q_PROPERTY(bool unresponsive READ isUnresponsive)

    /**
     * Whether this is a Wayland client.
     * @since 5.15
     */
    Q_PROPERTY(bool waylandClient READ isWaylandClient CONSTANT)

    /**
     * Whether this is an X11 client.
     * @since 5.15
     */
    Q_PROPERTY(bool x11Client READ isX11Client CONSTANT)

    /**
     * Whether the window is a popup.
     *
     * A popup is a window that can be used to implement tooltips, combo box popups,
     * popup menus and other similar user interface concepts.
     *
     * @since 5.15
     */
    Q_PROPERTY(bool popupWindow READ isPopupWindow CONSTANT)

    /**
     * KWin internal window. Specific to Wayland platform.
     *
     * If the EffectWindow does not reference an internal window, this property is @c null.
     * @since 5.16
     */
    Q_PROPERTY(QWindow* internalWindow READ internalWindow CONSTANT)

    /**
     * Whether this EffectWindow represents the outline.
     *
     * When compositing is turned on, the outline is an actual window.
     *
     * @since 5.16
     */
    Q_PROPERTY(bool outline READ isOutline CONSTANT)

    /**
     * The PID of the application this window belongs to.
     *
     * @since 5.18
     */
    Q_PROPERTY(pid_t pid READ pid CONSTANT)

    /**
     * Whether this EffectWindow represents the screenlocker greeter.
     *
     * @since 5.22
     */
    Q_PROPERTY(bool lockScreen READ isLockScreen CONSTANT)

public:
    /**  Flags explaining why painting should be disabled  */
    enum {
        /**  Window will not be painted  */
        PAINT_DISABLED = 1 << 0,
        /**  Window will not be painted because it is deleted  */
        PAINT_DISABLED_BY_DELETE = 1 << 1,
        /**  Window will not be painted because of which desktop it's on  */
        PAINT_DISABLED_BY_DESKTOP = 1 << 2,
        /**  Window will not be painted because it is minimized  */
        PAINT_DISABLED_BY_MINIMIZE = 1 << 3,
        /**  Window will not be painted because it's not on the current activity  */
        PAINT_DISABLED_BY_ACTIVITY = 1 << 4,
    };

    explicit EffectWindow(QObject* parent = nullptr);
    ~EffectWindow() override;

    virtual void enablePainting(int reason) = 0;
    virtual void disablePainting(int reason) = 0;
    virtual bool isPaintingEnabled() = 0;
    Q_SCRIPTABLE virtual void addRepaint(const QRect& r) = 0;
    Q_SCRIPTABLE virtual void addRepaint(int x, int y, int w, int h) = 0;
    Q_SCRIPTABLE virtual void addRepaintFull() = 0;
    Q_SCRIPTABLE virtual void addLayerRepaint(const QRect& r) = 0;
    Q_SCRIPTABLE virtual void addLayerRepaint(int x, int y, int w, int h) = 0;

    virtual void refWindow() = 0;
    virtual void unrefWindow() = 0;

    virtual bool isDeleted() const = 0;

    virtual bool isMinimized() const = 0;
    virtual double opacity() const = 0;

    bool isOnCurrentActivity() const;
    Q_SCRIPTABLE bool isOnActivity(const QString& id) const;
    bool isOnAllActivities() const;
    virtual QStringList activities() const = 0;

    Q_SCRIPTABLE bool isOnDesktop(int d) const;
    bool isOnCurrentDesktop() const;
    bool isOnAllDesktops() const;
    /**
     * All the desktops by number that the window is in. On X11 this list will always have
     * a length of 1, on Wayland can be any subset.
     * If the list is empty it means the window is on all desktops
     */
    virtual QVector<uint> desktops() const = 0;

    virtual int x() const = 0;
    virtual int y() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    /**
     * By how much the window wishes to grow/shrink at least. Usually QSize(1,1).
     * MAY BE DISOBEYED BY THE WM! It's only for information, do NOT rely on it at all.
     */
    virtual QSize basicUnit() const = 0;
    /**
     * Returns the geometry of the window excluding server-side and client-side
     * drop-shadows.
     *
     * @since 5.18
     */
    virtual QRect frameGeometry() const = 0;
    /**
     * Returns the geometry of the pixmap or buffer attached to this window.
     *
     * For X11 clients, this method returns server-side geometry of the Toplevel.
     *
     * For Wayland clients, this method returns rectangle that the main surface
     * occupies on the screen, in global screen coordinates.
     *
     * @since 5.18
     */
    virtual QRect bufferGeometry() const = 0;
    virtual QRect clientGeometry() const = 0;
    /**
     * Geometry of the window including decoration and potentially shadows.
     * May be different from geometry() if the window has a shadow.
     * @since 4.9
     */
    virtual QRect expandedGeometry() const = 0;
    virtual EffectScreen* screen() const = 0;
    virtual QPoint pos() const = 0;
    virtual QSize size() const = 0;
    virtual QRect rect() const = 0;
    virtual bool isMovable() const = 0;
    virtual bool isMovableAcrossScreens() const = 0;
    virtual bool isUserMove() const = 0;
    virtual bool isUserResize() const = 0;
    virtual QRect iconGeometry() const = 0;

    /**
     * Geometry of the actual window contents inside the whole (including decorations) window.
     */
    virtual QRect contentsRect() const = 0;
    /**
     * Geometry of the transparent rect in the decoration.
     * May be different from contentsRect() if the decoration is extended into the client area.
     * @since 4.5
     */
    virtual QRect decorationInnerRect() const = 0;
    bool hasDecoration() const;
    virtual bool decorationHasAlpha() const = 0;
    /**
     * Returns the decoration
     * @since 5.25
     */
    virtual KDecoration2::Decoration* decoration() const = 0;
    virtual QByteArray readProperty(long atom, long type, int format) const = 0;
    virtual void deleteProperty(long atom) const = 0;

    virtual QString caption() const = 0;
    virtual QIcon icon() const = 0;
    virtual QString windowClass() const = 0;
    virtual QString windowRole() const = 0;
    virtual const EffectWindowGroup* group() const = 0;

    /**
     * Returns whether the window is a desktop background window (the one with wallpaper).
     * See _NET_WM_WINDOW_TYPE_DESKTOP at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isDesktop() const = 0;
    /**
     * Returns whether the window is a dock (i.e. a panel).
     * See _NET_WM_WINDOW_TYPE_DOCK at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html
     * .
     */
    virtual bool isDock() const = 0;
    /**
     * Returns whether the window is a standalone (detached) toolbar window.
     * See _NET_WM_WINDOW_TYPE_TOOLBAR at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isToolbar() const = 0;
    /**
     * Returns whether the window is a torn-off menu.
     * See _NET_WM_WINDOW_TYPE_MENU at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html
     * .
     */
    virtual bool isMenu() const = 0;
    /**
     * Returns whether the window is a "normal" window, i.e. an application or any other window
     * for which none of the specialized window types fit. Normal as in 'NET::Normal or NET::Unknown
     * non-transient'. See _NET_WM_WINDOW_TYPE_NORMAL at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isNormalWindow() const = 0;
    /**
     * Returns whether the window is any of special windows types (desktop, dock, splash, ...),
     * i.e. window types that usually don't have a window frame and the user does not use window
     * management (moving, raising,...) on them.
     */
    virtual bool isSpecialWindow() const = 0;
    /**
     * Returns whether the window is a dialog window.
     * See _NET_WM_WINDOW_TYPE_DIALOG at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isDialog() const = 0;
    /**
     * Returns whether the window is a splashscreen. Note that many (especially older) applications
     * do not support marking their splash windows with this type.
     * See _NET_WM_WINDOW_TYPE_SPLASH at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isSplash() const = 0;
    /**
     * Returns whether the window is a utility window, such as a tool window.
     * See _NET_WM_WINDOW_TYPE_UTILITY at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isUtility() const = 0;
    /**
     * Returns whether the window is a dropdown menu (i.e. a popup directly or indirectly open
     * from the applications menubar).
     * See _NET_WM_WINDOW_TYPE_DROPDOWN_MENU at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isDropdownMenu() const = 0;
    /**
     * Returns whether the window is a popup menu (that is not a torn-off or dropdown menu).
     * See _NET_WM_WINDOW_TYPE_POPUP_MENU at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isPopupMenu() const = 0; // a context popup, not dropdown, not torn-off
    /**
     * Returns whether the window is a tooltip.
     * See _NET_WM_WINDOW_TYPE_TOOLTIP at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isTooltip() const = 0;
    /**
     * Returns whether the window is a window with a notification.
     * See _NET_WM_WINDOW_TYPE_NOTIFICATION at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isNotification() const = 0;
    /**
     * Returns whether the window is a window with a critical notification.
     * using the non-standard _KDE_NET_WM_WINDOW_TYPE_CRITICAL_NOTIFICATION
     */
    virtual bool isCriticalNotification() const = 0;
    /**
     * Returns whether the window is a window used for applet popups.
     */
    virtual bool isAppletPopup() const = 0;
    /**
     * Returns whether the window is an on screen display window
     * using the non-standard _KDE_NET_WM_WINDOW_TYPE_ON_SCREEN_DISPLAY
     */
    virtual bool isOnScreenDisplay() const = 0;
    /**
     * Returns whether the window is a combobox popup.
     * See _NET_WM_WINDOW_TYPE_COMBO at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool isComboBox() const = 0;
    /**
     * Returns whether the window is a Drag&Drop icon.
     * See _NET_WM_WINDOW_TYPE_DND at https://standards.freedesktop.org/wm-spec/wm-spec-latest.html
     * .
     */
    virtual bool isDNDIcon() const = 0;
    /**
     * Returns whether the window is managed by KWin (it has control over its placement and other
     * aspects, as opposed to override-redirect windows that are entirely handled by the
     * application).
     */
    virtual bool isManaged() const = 0; // whether it's managed or override-redirect
    /**
     * Returns whether or not the window can accept keyboard focus.
     */
    virtual bool acceptsFocus() const = 0;
    /**
     * Returns whether or not the window is kept above all other windows.
     */
    virtual bool keepAbove() const = 0;
    /**
     * Returns whether the window is kept below all other windows.
     */
    virtual bool keepBelow() const = 0;

    virtual bool isModal() const = 0;
    Q_SCRIPTABLE virtual KWin::EffectWindow* findModal() = 0;
    Q_SCRIPTABLE virtual KWin::EffectWindow* transientFor() = 0;
    Q_SCRIPTABLE virtual KWin::EffectWindowList mainWindows() const = 0;

    /**
     * Returns whether the window should be excluded from window switching effects.
     * @since 4.5
     */
    virtual bool isSkipSwitcher() const = 0;

    /**
     * Returns the unmodified window quad list. Can also be used to force rebuilding.
     */
    virtual WindowQuadList buildQuads(bool force = false) const = 0;

    void setMinimized(bool minimize);
    virtual void minimize() = 0;
    virtual void unminimize() = 0;
    Q_SCRIPTABLE virtual void closeWindow() = 0;

    /**
     * @since 4.11
     */
    bool isVisible() const;

    /**
     * @since 5.0
     */
    virtual bool skipsCloseAnimation() const = 0;

    /**
     * @since 5.5
     */
    virtual Wrapland::Server::Surface* surface() const = 0;

    /**
     * @since 5.6
     */
    virtual bool isFullScreen() const = 0;

    /**
     * @since 5.10
     */
    virtual bool isUnresponsive() const = 0;

    /**
     * @since 5.15
     */
    virtual bool isWaylandClient() const = 0;

    /**
     * @since 5.15
     */
    virtual bool isX11Client() const = 0;

    /**
     * @since 5.15
     */
    virtual bool isPopupWindow() const = 0;

    /**
     * @since 5.16
     */
    virtual QWindow* internalWindow() const = 0;

    /**
     * @since 5.16
     */
    virtual bool isOutline() const = 0;

    /**
     * @since 5.22
     */
    virtual bool isLockScreen() const = 0;

    /**
     * @since 5.18
     */
    virtual pid_t pid() const = 0;

    /// Well defined only on X11.
    virtual qlonglong windowId() const = 0;
    virtual QUuid internalId() const = 0;

    /**
     * Can be used to by effects to store arbitrary data in the EffectWindow.
     *
     * Invoking this method will emit the signal EffectsHandler::windowDataChanged.
     * @see EffectsHandler::windowDataChanged
     */
    Q_SCRIPTABLE virtual void setData(int role, const QVariant& data) = 0;
    Q_SCRIPTABLE virtual QVariant data(int role) const = 0;

    /**
     * @brief References the previous window pixmap to prevent discarding.
     *
     * This method allows to reference the previous window pixmap in case that a window changed
     * its size, which requires a new window pixmap. By referencing the previous (and then outdated)
     * window pixmap an effect can for example cross fade the current window pixmap with the
     * previous one. This allows for smoother transitions for window geometry changes.
     *
     * If an effect calls this method on a window it also needs to call
     * unreferencePreviousWindowPixmap once it does no longer need the previous window pixmap.
     *
     * Note: the window pixmap is not kept forever even when referenced. If the geometry changes
     * again, so that a new window pixmap is created, the previous window pixmap will be exchanged
     * with the current one. This means it's still possible to have rendering glitches. An effect is
     * supposed to track for itself the changes to the window's geometry and decide how the
     * transition should continue in such a situation.
     *
     * @see unreferencePreviousWindowPixmap
     * @since 4.11
     */
    virtual void referencePreviousWindowPixmap() = 0;
    /**
     * @brief Unreferences the previous window pixmap. Only relevant after
     * referencePreviousWindowPixmap had been called.
     *
     * @see referencePreviousWindowPixmap
     * @since 4.11
     */
    virtual void unreferencePreviousWindowPixmap() = 0;

private:
    class Private;
    QScopedPointer<Private> d;
};

}

Q_DECLARE_METATYPE(KWin::EffectWindow*)
Q_DECLARE_METATYPE(KWin::EffectWindowList)
