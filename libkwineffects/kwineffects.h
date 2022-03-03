/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>
Copyright (C) 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#ifndef KWINEFFECTS_H
#define KWINEFFECTS_H

#include <kwineffects/effect.h>
#include <kwineffects/effect_plugin_factory.h>
#include <kwineffects/effect_screen.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/types.h>

#include <kwinconfig.h>
#include <kwineffects_export.h>
#include <kwinglobals.h>

#include <QEasingCurve>
#include <QIcon>
#include <QPair>
#include <QRect>
#include <QRegion>
#include <QSet>
#include <QVector2D>
#include <QVector3D>

#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QScopedPointer>
#include <QStack>
#include <QVector>

#include <netwm.h>

#include <climits>
#include <functional>

class KConfigGroup;
class QFont;
class QMatrix4x4;

namespace KWin
{

class PaintDataPrivate;
class WindowPaintDataPrivate;

class EffectWindowGroup;
class EffectFramePrivate;
class WindowQuad;
class GLShader;
class XRenderPicture;
class WindowQuadList;
class ScreenPaintData;

/** @defgroup kwineffects KWin effects library
 * KWin effects library contains necessary classes for creating new KWin
 * compositing effects.
 *
 * @section creating Creating new effects
 * This example will demonstrate the basics of creating an effect. We'll use
 *  CoolEffect as the class name, cooleffect as internal name and
 *  "Cool Effect" as user-visible name of the effect.
 *
 * This example doesn't demonstrate how to write the effect's code. For that,
 *  see the documentation of the Effect class.
 *
 * @subsection creating-class CoolEffect class
 * First you need to create CoolEffect class which has to be a subclass of
 *  @ref KWin::Effect. In that class you can reimplement various virtual
 *  methods to control how and where the windows are drawn.
 *
 * @subsection creating-macro KWIN_EFFECT_FACTORY macro
 * This library provides a specialized KPluginFactory subclass and macros to
 * create a sub class. This subclass of KPluginFactory has to be used, otherwise
 * KWin won't load the plugin. Use the @ref KWIN_EFFECT_FACTORY macro to create the
 * plugin factory.
 *
 * @subsection creating-buildsystem Buildsystem
 * To build the effect, you can use the KWIN_ADD_EFFECT() cmake macro which
 *  can be found in effects/CMakeLists.txt file in KWin's source. First
 *  argument of the macro is the name of the library that will contain
 *  your effect. Although not strictly required, it is usually a good idea to
 *  use the same name as your effect's internal name there. Following arguments
 *  to the macro are the files containing your effect's source. If our effect's
 *  source is in cooleffect.cpp, we'd use following:
 * @code
 *  KWIN_ADD_EFFECT(cooleffect cooleffect.cpp)
 * @endcode
 *
 * This macro takes care of compiling your effect. You'll also need to install
 *  your effect's .desktop file, so the example CMakeLists.txt file would be
 *  as follows:
 * @code
 *  KWIN_ADD_EFFECT(cooleffect cooleffect.cpp)
 *  install( FILES cooleffect.desktop DESTINATION ${SERVICES_INSTALL_DIR}/kwin )
 * @endcode
 *
 * @subsection creating-desktop Effect's .desktop file
 * You will also need to create .desktop file to set name, description, icon
 *  and other properties of your effect. Important fields of the .desktop file
 *  are:
 *  @li Name User-visible name of your effect
 *  @li Icon Name of the icon of the effect
 *  @li Comment Short description of the effect
 *  @li Type must be "Service"
 *  @li X-KDE-ServiceTypes must be "KWin/Effect" for scripted effects
 *  @li X-KDE-PluginInfo-Name effect's internal name as passed to the KWIN_EFFECT macro plus
 *      "kwin4_effect_" prefix
 *  @li X-KDE-PluginInfo-Category effect's category. Should be one of Appearance, Accessibility,
 *      Window Management, Demos, Tests, Misc
 *  @li X-KDE-PluginInfo-EnabledByDefault whether the effect should be enabled by default (use
 *      sparingly). Default is false
 *  @li X-KDE-Library name of the library containing the effect. This is the first argument passed
 *      to the KWIN_ADD_EFFECT macro in cmake file plus "kwin4_effect_" prefix.
 *
 * Example cooleffect.desktop file follows:
 * @code
 * [Desktop Entry]
 * Name=Cool Effect
 * Comment=The coolest effect you've ever seen
 * Icon=preferences-system-windows-effect-cooleffect
 *
 * Type=Service
 * X-KDE-ServiceTypes=KWin/Effect
 * X-KDE-PluginInfo-Author=My Name
 * X-KDE-PluginInfo-Email=my@email.here
 * X-KDE-PluginInfo-Name=kwin4_effect_cooleffect
 * X-KDE-PluginInfo-Category=Misc
 * X-KDE-Library=kwin4_effect_cooleffect
 * @endcode
 *
 *
 * @section accessing Accessing windows and workspace
 * Effects can gain access to the properties of windows and workspace via
 *  EffectWindow and EffectsHandler classes.
 *
 * There is one global EffectsHandler object which you can access using the
 *  @ref effects pointer.
 * For each window, there is an EffectWindow object which can be used to read
 *  window properties such as position and also to change them.
 *
 * For more information about this, see the documentation of the corresponding
 *  classes.
 *
 * @{
 */

/**
 * @short Representation of a window used by/for Effect classes.
 *
 * The purpose is to hide internal data and also to serve as a single
 *  representation for the case when Client/Unmanaged becomes Deleted.
 */
class KWINEFFECTS_EXPORT EffectWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool alpha READ hasAlpha CONSTANT)
    Q_PROPERTY(QRect geometry READ geometry)
    Q_PROPERTY(QRect expandedGeometry READ expandedGeometry)
    Q_PROPERTY(int height READ height)
    Q_PROPERTY(qreal opacity READ opacity)
    Q_PROPERTY(QPoint pos READ pos)
    Q_PROPERTY(int screen READ screen)
    Q_PROPERTY(QSize size READ size)
    Q_PROPERTY(int width READ width)
    Q_PROPERTY(int x READ x)
    Q_PROPERTY(int y READ y)
    Q_PROPERTY(int desktop READ desktop)
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
     * Returns the NETWM window type
     * See https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    Q_PROPERTY(int windowType READ windowType)
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
    Q_PROPERTY(bool outline READ isOutline CONSTANT)

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
        /**  Deprecated, tab groups have been removed: Window will not be painted because it is not
           the active window in a client group */
        PAINT_DISABLED_BY_TAB_GROUP = 1 << 4,
        /**  Window will not be painted because it's not on the current activity  */
        PAINT_DISABLED_BY_ACTIVITY = 1 << 5
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
    virtual bool hasAlpha() const = 0;

    bool isOnCurrentActivity() const;
    Q_SCRIPTABLE bool isOnActivity(const QString& id) const;
    bool isOnAllActivities() const;
    virtual QStringList activities() const = 0;

    Q_SCRIPTABLE bool isOnDesktop(int d) const;
    bool isOnCurrentDesktop() const;
    bool isOnAllDesktops() const;
    /**
     * The desktop this window is in. This makes sense only on X11
     * where desktops are mutually exclusive, on Wayland it's the last
     * desktop the window has been added to.
     * use desktops() instead.
     * @see desktops()
     * @deprecated
     */
#ifndef KWIN_NO_DEPRECATED
    virtual int KWIN_DEPRECATED desktop() const = 0; // prefer isOnXXX()
#endif
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
     * @deprecated Use frameGeometry() instead.
     */
    virtual QRect KWIN_DEPRECATED geometry() const = 0;
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
    virtual int screen() const = 0;
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
     * for which none of the specialized window types fit.
     * See _NET_WM_WINDOW_TYPE_NORMAL at
     * https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual bool
    isNormalWindow() const = 0; // normal as in 'NET::Normal or NET::Unknown non-transient'
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
     * Returns the NETWM window type
     * See https://standards.freedesktop.org/wm-spec/wm-spec-latest.html .
     */
    virtual NET::WindowType windowType() const = 0;
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

    /// deprecated
    virtual bool isCurrentTab() const = 0;

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

class KWINEFFECTS_EXPORT EffectWindowGroup
{
public:
    virtual ~EffectWindowGroup();
    virtual EffectWindowList members() const = 0;
};

struct GLVertex2D {
    QVector2D position;
    QVector2D texcoord;
};

struct GLVertex3D {
    QVector3D position;
    QVector2D texcoord;
};

/**
 * @short Vertex class
 *
 * A vertex is one position in a window. WindowQuad consists of four WindowVertex objects
 * and represents one part of a window.
 */
class KWINEFFECTS_EXPORT WindowVertex
{
public:
    WindowVertex();
    WindowVertex(const QPointF& position, const QPointF& textureCoordinate);
    WindowVertex(double x, double y, double tx, double ty);

    double x() const
    {
        return px;
    }
    double y() const
    {
        return py;
    }
    double u() const
    {
        return tx;
    }
    double v() const
    {
        return ty;
    }
    double originalX() const
    {
        return ox;
    }
    double originalY() const
    {
        return oy;
    }
    double textureX() const
    {
        return tx;
    }
    double textureY() const
    {
        return ty;
    }
    void move(double x, double y);
    void setX(double x);
    void setY(double y);

private:
    friend class WindowQuad;
    friend class WindowQuadList;
    double px, py; // position
    double ox, oy; // origional position
    double tx, ty; // texture coords
};

/**
 * @short Class representing one area of a window.
 *
 * WindowQuads consists of four WindowVertex objects and represents one part of a window.
 */
// NOTE: This class expects the (original) vertices to be in the clockwise order starting from
// topleft.
class KWINEFFECTS_EXPORT WindowQuad
{
public:
    explicit WindowQuad(WindowQuadType type, int id = -1);
    WindowQuad makeSubQuad(double x1, double y1, double x2, double y2) const;
    WindowVertex& operator[](int index);
    const WindowVertex& operator[](int index) const;
    WindowQuadType type() const;
    void setUVAxisSwapped(bool value)
    {
        uvSwapped = value;
    }
    bool uvAxisSwapped() const
    {
        return uvSwapped;
    }
    int id() const;
    bool decoration() const;
    bool effect() const;
    double left() const;
    double right() const;
    double top() const;
    double bottom() const;
    double originalLeft() const;
    double originalRight() const;
    double originalTop() const;
    double originalBottom() const;
    bool smoothNeeded() const;
    bool isTransformed() const;

private:
    friend class WindowQuadList;
    WindowVertex verts[4];
    WindowQuadType quadType; // 0 - contents, 1 - decoration
    bool uvSwapped;
    int quadID;
};

class KWINEFFECTS_EXPORT WindowQuadList : public QVector<WindowQuad>
{
public:
    WindowQuadList splitAtX(double x) const;
    WindowQuadList splitAtY(double y) const;
    WindowQuadList makeGrid(int maxquadsize) const;
    WindowQuadList makeRegularGrid(int xSubdivisions, int ySubdivisions) const;
    WindowQuadList select(WindowQuadType type) const;
    WindowQuadList filterOut(WindowQuadType type) const;
    bool smoothNeeded() const;
    void
    makeInterleavedArrays(unsigned int type, GLVertex2D* vertices, const QMatrix4x4& matrix) const;
    void makeArrays(float** vertices, float** texcoords, const QSizeF& size, bool yInverted) const;
    bool isTransformed() const;
};

class KWINEFFECTS_EXPORT WindowPrePaintData
{
public:
    int mask;
    /**
     * Region that will be painted, in screen coordinates.
     */
    QRegion paint;
    /**
     * The clip region will be subtracted from paint region of following windows.
     * I.e. window will definitely cover it's clip region
     */
    QRegion clip;
    WindowQuadList quads;
    /**
     * Simple helper that sets data to say the window will be painted as non-opaque.
     * Takes also care of changing the regions.
     */
    void setTranslucent();
    /**
     * Helper to mark that this window will be transformed
     */
    void setTransformed();
};

class KWINEFFECTS_EXPORT PaintData
{
public:
    virtual ~PaintData();
    /**
     * @returns scale factor in X direction.
     * @since 4.10
     */
    qreal xScale() const;
    /**
     * @returns scale factor in Y direction.
     * @since 4.10
     */
    qreal yScale() const;
    /**
     * @returns scale factor in Z direction.
     * @since 4.10
     */
    qreal zScale() const;
    /**
     * Sets the scale factor in X direction to @p scale
     * @param scale The scale factor in X direction
     * @since 4.10
     */
    void setXScale(qreal scale);
    /**
     * Sets the scale factor in Y direction to @p scale
     * @param scale The scale factor in Y direction
     * @since 4.10
     */
    void setYScale(qreal scale);
    /**
     * Sets the scale factor in Z direction to @p scale
     * @param scale The scale factor in Z direction
     * @since 4.10
     */
    void setZScale(qreal scale);
    /**
     * Sets the scale factor in X and Y direction.
     * @param scale The scale factor for X and Y direction
     * @since 4.10
     */
    void setScale(const QVector2D& scale);
    /**
     * Sets the scale factor in X, Y and Z direction
     * @param scale The scale factor for X, Y and Z direction
     * @since 4.10
     */
    void setScale(const QVector3D& scale);
    const QVector3D& scale() const;
    const QVector3D& translation() const;
    /**
     * @returns the translation in X direction.
     * @since 4.10
     */
    qreal xTranslation() const;
    /**
     * @returns the translation in Y direction.
     * @since 4.10
     */
    qreal yTranslation() const;
    /**
     * @returns the translation in Z direction.
     * @since 4.10
     */
    qreal zTranslation() const;
    /**
     * Sets the translation in X direction to @p translate.
     * @since 4.10
     */
    void setXTranslation(qreal translate);
    /**
     * Sets the translation in Y direction to @p translate.
     * @since 4.10
     */
    void setYTranslation(qreal translate);
    /**
     * Sets the translation in Z direction to @p translate.
     * @since 4.10
     */
    void setZTranslation(qreal translate);
    /**
     * Performs a translation by adding the values component wise.
     * @param x Translation in X direction
     * @param y Translation in Y direction
     * @param z Translation in Z direction
     * @since 4.10
     */
    void translate(qreal x, qreal y = 0.0, qreal z = 0.0);
    /**
     * Performs a translation by adding the values component wise.
     * Overloaded method for convenience.
     * @param translate The translation
     * @since 4.10
     */
    void translate(const QVector3D& translate);

    /**
     * Sets the rotation angle.
     * @param angle The new rotation angle.
     * @since 4.10
     * @see rotationAngle()
     */
    void setRotationAngle(qreal angle);
    /**
     * Returns the rotation angle.
     * Initially 0.0.
     * @returns The current rotation angle.
     * @since 4.10
     * @see setRotationAngle
     */
    qreal rotationAngle() const;
    /**
     * Sets the rotation origin.
     * @param origin The new rotation origin.
     * @since 4.10
     * @see rotationOrigin()
     */
    void setRotationOrigin(const QVector3D& origin);
    /**
     * Returns the rotation origin. That is the point in space which is fixed during the rotation.
     * Initially this is 0/0/0.
     * @returns The rotation's origin
     * @since 4.10
     * @see setRotationOrigin()
     */
    QVector3D rotationOrigin() const;
    /**
     * Sets the rotation axis.
     * Set a component to 1.0 to rotate around this axis and to 0.0 to disable rotation around the
     * axis.
     * @param axis A vector holding information on which axis to rotate
     * @since 4.10
     * @see rotationAxis()
     */
    void setRotationAxis(const QVector3D& axis);
    /**
     * Sets the rotation axis.
     * Overloaded method for convenience.
     * @param axis The axis around which should be rotated.
     * @since 4.10
     * @see rotationAxis()
     */
    void setRotationAxis(Qt::Axis axis);
    /**
     * The current rotation axis.
     * By default the rotation is (0/0/1) which means a rotation around the z axis.
     * @returns The current rotation axis.
     * @since 4.10
     * @see setRotationAxis
     */
    QVector3D rotationAxis() const;

protected:
    PaintData();
    PaintData(const PaintData& other);

private:
    PaintDataPrivate* const d;
};

class KWINEFFECTS_EXPORT WindowPaintData : public PaintData
{
public:
    explicit WindowPaintData(EffectWindow* w);
    explicit WindowPaintData(EffectWindow* w, const QMatrix4x4& screenProjectionMatrix);
    WindowPaintData(const WindowPaintData& other);
    ~WindowPaintData() override;
    /**
     * Scales the window by @p scale factor.
     * Multiplies all three components by the given factor.
     * @since 4.10
     */
    WindowPaintData& operator*=(qreal scale);
    /**
     * Scales the window by @p scale factor.
     * Performs a component wise multiplication on x and y components.
     * @since 4.10
     */
    WindowPaintData& operator*=(const QVector2D& scale);
    /**
     * Scales the window by @p scale factor.
     * Performs a component wise multiplication.
     * @since 4.10
     */
    WindowPaintData& operator*=(const QVector3D& scale);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QPointF& translation);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QPoint& translation);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QVector2D& translation);
    /**
     * Translates the window by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    WindowPaintData& operator+=(const QVector3D& translation);
    /**
     * Window opacity, in range 0 = transparent to 1 = fully opaque
     * @see setOpacity
     * @since 4.10
     */
    qreal opacity() const;
    /**
     * Sets the window opacity to the new @p opacity.
     * If you want to modify the existing opacity level consider using multiplyOpacity.
     * @param opacity The new opacity level
     * @since 4.10
     */
    void setOpacity(qreal opacity);
    /**
     * Multiplies the current opacity with the @p factor.
     * @param factor Factor with which the opacity should be multiplied
     * @return New opacity level
     * @since 4.10
     */
    qreal multiplyOpacity(qreal factor);
    /**
     * Saturation of the window, in range [0; 1]
     * 1 means that the window is unchanged, 0 means that it's completely
     *  unsaturated (greyscale). 0.5 would make the colors less intense,
     *  but not completely grey
     * Use EffectsHandler::saturationSupported() to find out whether saturation
     * is supported by the system, otherwise this value has no effect.
     * @return The current saturation
     * @see setSaturation()
     * @since 4.10
     */
    qreal saturation() const;
    /**
     * Sets the window saturation level to @p saturation.
     * If you want to modify the existing saturation level consider using multiplySaturation.
     * @param saturation The new saturation level
     * @since 4.10
     */
    void setSaturation(qreal saturation) const;
    /**
     * Multiplies the current saturation with @p factor.
     * @param factor with which the saturation should be multiplied
     * @return New saturation level
     * @since 4.10
     */
    qreal multiplySaturation(qreal factor);
    /**
     * Brightness of the window, in range [0; 1]
     * 1 means that the window is unchanged, 0 means that it's completely
     * black. 0.5 would make it 50% darker than usual
     */
    qreal brightness() const;
    /**
     * Sets the window brightness level to @p brightness.
     * If you want to modify the existing brightness level consider using multiplyBrightness.
     * @param brightness The new brightness level
     */
    void setBrightness(qreal brightness);
    /**
     * Multiplies the current brightness level with @p factor.
     * @param factor with which the brightness should be multiplied.
     * @return New brightness level
     * @since 4.10
     */
    qreal multiplyBrightness(qreal factor);
    /**
     * The screen number for which the painting should be done.
     * This affects color correction (different screens may need different
     * color correction lookup tables because they have different ICC profiles).
     * @return screen for which painting should be done
     */
    int screen() const;
    /**
     * @param screen New screen number
     * A value less than 0 will indicate that a default profile should be done.
     */
    void setScreen(int screen) const;
    /**
     * @brief Sets the cross fading @p factor to fade over with previously sized window.
     * If @c 1.0 only the current window is used, if @c 0.0 only the previous window is used.
     *
     * By default only the current window is used. This factor can only make any visual difference
     * if the previous window get referenced.
     *
     * @param factor The cross fade factor between @c 0.0 (previous window) and @c 1.0 (current
     * window)
     * @see crossFadeProgress
     */
    void setCrossFadeProgress(qreal factor);
    /**
     * @see setCrossFadeProgress
     */
    qreal crossFadeProgress() const;

    /**
     * Sets the projection matrix that will be used when painting the window.
     *
     * The default projection matrix can be overridden by setting this matrix
     * to a non-identity matrix.
     */
    void setProjectionMatrix(const QMatrix4x4& matrix);

    /**
     * Returns the current projection matrix.
     *
     * The default value for this matrix is the identity matrix.
     */
    QMatrix4x4 projectionMatrix() const;

    /**
     * Returns a reference to the projection matrix.
     */
    QMatrix4x4& rprojectionMatrix();

    /**
     * Sets the model-view matrix that will be used when painting the window.
     *
     * The default model-view matrix can be overridden by setting this matrix
     * to a non-identity matrix.
     */
    void setModelViewMatrix(const QMatrix4x4& matrix);

    /**
     * Returns the current model-view matrix.
     *
     * The default value for this matrix is the identity matrix.
     */
    QMatrix4x4 modelViewMatrix() const;

    /**
     * Returns a reference to the model-view matrix.
     */
    QMatrix4x4& rmodelViewMatrix();

    /**
     * Returns The projection matrix as used by the current screen painting pass
     * including screen transformations.
     *
     * @since 5.6
     */
    QMatrix4x4 screenProjectionMatrix() const;

    WindowQuadList quads;

    /**
     * Shader to be used for rendering, if any.
     */
    GLShader* shader;

private:
    WindowPaintDataPrivate* const d;
};

class KWINEFFECTS_EXPORT ScreenPaintData : public PaintData
{
public:
    ScreenPaintData();
    ScreenPaintData(const QMatrix4x4& projectionMatrix, EffectScreen* screen = nullptr);
    ScreenPaintData(const ScreenPaintData& other);
    ~ScreenPaintData() override;
    /**
     * Scales the screen by @p scale factor.
     * Multiplies all three components by the given factor.
     * @since 4.10
     */
    ScreenPaintData& operator*=(qreal scale);
    /**
     * Scales the screen by @p scale factor.
     * Performs a component wise multiplication on x and y components.
     * @since 4.10
     */
    ScreenPaintData& operator*=(const QVector2D& scale);
    /**
     * Scales the screen by @p scale factor.
     * Performs a component wise multiplication.
     * @since 4.10
     */
    ScreenPaintData& operator*=(const QVector3D& scale);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QPointF& translation);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QPoint& translation);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QVector2D& translation);
    /**
     * Translates the screen by the given @p translation and returns a reference to the
     * ScreenPaintData. Overloaded method for convenience.
     * @since 4.10
     */
    ScreenPaintData& operator+=(const QVector3D& translation);
    ScreenPaintData& operator=(const ScreenPaintData& rhs);

    /**
     * The projection matrix used by the scene for the current rendering pass.
     * On non-OpenGL compositors it's set to Identity matrix.
     * @since 5.6
     */
    QMatrix4x4 projectionMatrix() const;

    /**
     * Returns the currently rendered screen. Only set for per-screen rendering, e.g. Wayland.
     */
    EffectScreen* screen() const;

private:
    class Private;
    QScopedPointer<Private> d;
};

class KWINEFFECTS_EXPORT ScreenPrePaintData
{
public:
    int mask;
    QRegion paint;
};

/**
 * @short Helper class for restricting painting area only to allowed area.
 *
 * This helper class helps specifying areas that should be painted, clipping
 * out the rest. The simplest usage is creating an object on the stack
 * and giving it the area that is allowed to be painted to. When the object
 * is destroyed, the restriction will be removed.
 * Note that all painting code must use paintArea() to actually perform the clipping.
 */
class KWINEFFECTS_EXPORT PaintClipper
{
public:
    /**
     * Calls push().
     */
    explicit PaintClipper(const QRegion& allowed_area);
    /**
     * Calls pop().
     */
    ~PaintClipper();
    /**
     * Allows painting only in the given area. When areas have been already
     * specified, painting is allowed only in the intersection of all areas.
     */
    static void push(const QRegion& allowed_area);
    /**
     * Removes the given area. It must match the top item in the stack.
     */
    static void pop(const QRegion& allowed_area);
    /**
     * Returns true if any clipping should be performed.
     */
    static bool clip();
    /**
     * If clip() returns true, this function gives the resulting area in which
     * painting is allowed. It is usually simpler to use the helper Iterator class.
     */
    static QRegion paintArea();
    /**
     * Helper class to perform the clipped painting. The usage is:
     * @code
     * for ( PaintClipper::Iterator iterator;
     *      !iterator.isDone();
     *      iterator.next())
     *     { // do the painting, possibly use iterator.boundingRect()
     *     }
     * @endcode
     */
    class KWINEFFECTS_EXPORT Iterator
    {
    public:
        Iterator();
        ~Iterator();
        bool isDone();
        void next();
        QRect boundingRect() const;

    private:
        struct Data;
        Data* data;
    };

private:
    QRegion area;
    static QStack<QRegion>* areas;
};

/**
 * @internal
 */
template<typename T>
class KWINEFFECTS_EXPORT Motion
{
public:
    /**
     * Creates a new motion object. "Strength" is the amount of
     * acceleration that is applied to the object when the target
     * changes and "smoothness" relates to how fast the object
     * can change its direction and speed.
     */
    explicit Motion(T initial, double strength, double smoothness);
    /**
     * Creates an exact copy of another motion object, including
     * position, target and velocity.
     */
    Motion(const Motion<T>& other);
    ~Motion();

    inline T value() const
    {
        return m_value;
    }
    inline void setValue(const T value)
    {
        m_value = value;
    }
    inline T target() const
    {
        return m_target;
    }
    inline void setTarget(const T target)
    {
        m_start = m_value;
        m_target = target;
    }
    inline T velocity() const
    {
        return m_velocity;
    }
    inline void setVelocity(const T velocity)
    {
        m_velocity = velocity;
    }

    inline double strength() const
    {
        return m_strength;
    }
    inline void setStrength(const double strength)
    {
        m_strength = strength;
    }
    inline double smoothness() const
    {
        return m_smoothness;
    }
    inline void setSmoothness(const double smoothness)
    {
        m_smoothness = smoothness;
    }
    inline T startValue()
    {
        return m_start;
    }

    /**
     * The distance between the current position and the target.
     */
    inline T distance() const
    {
        return m_target - m_value;
    }

    /**
     * Calculates the new position if not at the target. Called
     * once per frame only.
     */
    void calculate(const int msec);
    /**
     * Place the object on top of the target immediately,
     * bypassing all movement calculation.
     */
    void finish();

private:
    T m_value;
    T m_start;
    T m_target;
    T m_velocity;
    double m_strength;
    double m_smoothness;
};

/**
 * @short A single 1D motion dynamics object.
 *
 * This class represents a single object that can be moved around a
 * 1D space. Although it can be used directly by itself it is
 * recommended to use a motion manager instead.
 */
class KWINEFFECTS_EXPORT Motion1D : public Motion<double>
{
public:
    explicit Motion1D(double initial = 0.0, double strength = 0.08, double smoothness = 4.0);
    Motion1D(const Motion1D& other);
    ~Motion1D();
};

/**
 * @short A single 2D motion dynamics object.
 *
 * This class represents a single object that can be moved around a
 * 2D space. Although it can be used directly by itself it is
 * recommended to use a motion manager instead.
 */
class KWINEFFECTS_EXPORT Motion2D : public Motion<QPointF>
{
public:
    explicit Motion2D(QPointF initial = QPointF(), double strength = 0.08, double smoothness = 4.0);
    Motion2D(const Motion2D& other);
    ~Motion2D();
};

/**
 * @short Helper class for motion dynamics in KWin effects.
 *
 * This motion manager class is intended to help KWin effect authors
 * move windows across the screen smoothly and naturally. Once
 * windows are registered by the manager the effect can issue move
 * commands with the moveWindow() methods. The position of any
 * managed window can be determined in realtime by the
 * transformedGeometry() method. As the manager knows if any windows
 * are moving at any given time it can also be used as a notifier as
 * to see whether the effect is active or not.
 */
class KWINEFFECTS_EXPORT WindowMotionManager
{
public:
    /**
     * Creates a new window manager object.
     */
    explicit WindowMotionManager(bool useGlobalAnimationModifier = true);
    ~WindowMotionManager();

    /**
     * Register a window for managing.
     */
    void manage(EffectWindow* w);
    /**
     * Register a list of windows for managing.
     */
    inline void manage(const EffectWindowList& list)
    {
        for (int i = 0; i < list.size(); i++)
            manage(list.at(i));
    }
    /**
     * Deregister a window. All transformations applied to the
     * window will be permanently removed and cannot be recovered.
     */
    void unmanage(EffectWindow* w);
    /**
     * Deregister all windows, returning the manager to its
     * originally initiated state.
     */
    void unmanageAll();
    /**
     * Determine the new positions for windows that have not
     * reached their target. Called once per frame, usually in
     * prePaintScreen(). Remember to set the
     * Effect::PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS flag.
     */
    void calculate(int time);
    /**
     * Modify a registered window's paint data to make it appear
     * at its real location on the screen. Usually called in
     * paintWindow(). Remember to flag the window as having been
     * transformed in prePaintWindow() by calling
     * WindowPrePaintData::setTransformed()
     */
    void apply(EffectWindow* w, WindowPaintData& data);
    /**
     * Set all motion targets and values back to where the
     * windows were before transformations. The same as
     * unmanaging then remanaging all windows.
     */
    void reset();
    /**
     * Resets the motion target and current value of a single
     * window.
     */
    void reset(EffectWindow* w);

    /**
     * Ask the manager to move the window to the target position
     * with the specified scale. If `yScale` is not provided or
     * set to 0.0, `scale` will be used as the scale in the
     * vertical direction as well as in the horizontal direction.
     */
    void moveWindow(EffectWindow* w, QPoint target, double scale = 1.0, double yScale = 0.0);
    /**
     * This is an overloaded method, provided for convenience.
     *
     * Ask the manager to move the window to the target rectangle.
     * Automatically determines scale.
     */
    inline void moveWindow(EffectWindow* w, QRect target)
    {
        // TODO: Scale might be slightly different in the comparison due to rounding
        moveWindow(w,
                   target.topLeft(),
                   target.width() / double(w->width()),
                   target.height() / double(w->height()));
    }

    /**
     * Retrieve the current tranformed geometry of a registered
     * window.
     */
    QRectF transformedGeometry(EffectWindow* w) const;
    /**
     * Sets the current transformed geometry of a registered window to the given geometry.
     * @see transformedGeometry
     * @since 4.5
     */
    void setTransformedGeometry(EffectWindow* w, const QRectF& geometry);
    /**
     * Retrieve the current target geometry of a registered
     * window.
     */
    QRectF targetGeometry(EffectWindow* w) const;
    /**
     * Return the window that has its transformed geometry under
     * the specified point. It is recommended to use the stacking
     * order as it's what the user sees, but it is slightly
     * slower to process.
     */
    EffectWindow* windowAtPoint(QPoint point, bool useStackingOrder = true) const;

    /**
     * Return a list of all currently registered windows.
     */
    inline EffectWindowList managedWindows() const
    {
        return m_managedWindows.keys();
    }
    /**
     * Returns whether or not a specified window is being managed
     * by this manager object.
     */
    inline bool isManaging(EffectWindow* w) const
    {
        return m_managedWindows.contains(w);
    }
    /**
     * Returns whether or not this manager object is actually
     * managing any windows or not.
     */
    inline bool managingWindows() const
    {
        return !m_managedWindows.empty();
    }
    /**
     * Returns whether all windows have reached their targets yet
     * or not. Can be used to see if an effect should be
     * processed and displayed or not.
     */
    inline bool areWindowsMoving() const
    {
        return !m_movingWindowsSet.isEmpty();
    }
    /**
     * Returns whether a window has reached its targets yet
     * or not.
     */
    inline bool isWindowMoving(EffectWindow* w) const
    {
        return m_movingWindowsSet.contains(w);
    }

private:
    bool m_useGlobalAnimationModifier;
    struct WindowMotion {
        // TODO: Rotation, etc?
        Motion2D translation; // Absolute position
        Motion2D scale;       // xScale and yScale
    };
    QHash<EffectWindow*, WindowMotion> m_managedWindows;
    QSet<EffectWindow*> m_movingWindowsSet;
};

/**
 * @short Helper class for displaying text and icons in frames.
 *
 * Paints text and/or and icon with an optional frame around them. The
 * available frames includes one that follows the default Plasma theme and
 * another that doesn't.
 * It is recommended to use this class whenever displaying text.
 */
class KWINEFFECTS_EXPORT EffectFrame
{
public:
    EffectFrame();
    virtual ~EffectFrame();

    /**
     * Delete any existing textures to free up graphics memory. They will
     * be automatically recreated the next time they are required.
     */
    virtual void free() = 0;

    /**
     * Render the frame.
     */
    virtual void render(const QRegion& region = infiniteRegion(),
                        double opacity = 1.0,
                        double frameOpacity = 1.0)
        = 0;

    virtual void setPosition(const QPoint& point) = 0;
    /**
     * Set the text alignment for static frames and the position alignment
     * for non-static.
     */
    virtual void setAlignment(Qt::Alignment alignment) = 0;
    virtual Qt::Alignment alignment() const = 0;
    virtual void setGeometry(const QRect& geometry, bool force = false) = 0;
    virtual const QRect& geometry() const = 0;

    virtual void setText(const QString& text) = 0;
    virtual const QString& text() const = 0;
    virtual void setFont(const QFont& font) = 0;
    virtual const QFont& font() const = 0;
    /**
     * Set the icon that will appear on the left-hand size of the frame.
     */
    virtual void setIcon(const QIcon& icon) = 0;
    virtual const QIcon& icon() const = 0;
    virtual void setIconSize(const QSize& size) = 0;
    virtual const QSize& iconSize() const = 0;

    /**
     * Sets the geometry of a selection.
     * To remove the selection set a null rect.
     * @param selection The geometry of the selection in screen coordinates.
     */
    virtual void setSelection(const QRect& selection) = 0;

    /**
     * @param shader The GLShader for rendering.
     */
    virtual void setShader(GLShader* shader) = 0;
    /**
     * @returns The GLShader used for rendering or null if none.
     */
    virtual GLShader* shader() const = 0;

    /**
     * @returns The style of this EffectFrame.
     */
    virtual EffectFrameStyle style() const = 0;

    /**
     * If @p enable is @c true cross fading between icons and text is enabled
     * By default disabled. Use setCrossFadeProgress to cross fade.
     * Cross Fading is currently only available if OpenGL is used.
     * @param enable @c true enables cross fading, @c false disables it again
     * @see isCrossFade
     * @see setCrossFadeProgress
     * @since 4.6
     */
    void enableCrossFade(bool enable);
    /**
     * @returns @c true if cross fading is enabled, @c false otherwise
     * @see enableCrossFade
     * @since 4.6
     */
    bool isCrossFade() const;
    /**
     * Sets the current progress for cross fading the last used icon/text
     * with current icon/text to @p progress.
     * A value of 0.0 means completely old icon/text, a value of 1.0 means
     * completely current icon/text.
     * Default value is 1.0. You have to enable cross fade before using it.
     * Cross Fading is currently only available if OpenGL is used.
     * @see enableCrossFade
     * @see isCrossFade
     * @see crossFadeProgress
     * @since 4.6
     */
    void setCrossFadeProgress(qreal progress);
    /**
     * @returns The current progress for cross fading
     * @see setCrossFadeProgress
     * @see enableCrossFade
     * @see isCrossFade
     * @since 4.6
     */
    qreal crossFadeProgress() const;

    /**
     * Returns The projection matrix as used by the current screen painting pass
     * including screen transformations.
     *
     * This matrix is only valid during a rendering pass started by render.
     *
     * @since 5.6
     * @see render
     * @see EffectsHandler::paintEffectFrame
     * @see Effect::paintEffectFrame
     */
    QMatrix4x4 screenProjectionMatrix() const;

protected:
    void setScreenProjectionMatrix(const QMatrix4x4& projection);

private:
    EffectFramePrivate* const d;
};

/**
 * The TimeLine class is a helper for controlling animations.
 */
class KWINEFFECTS_EXPORT TimeLine
{
public:
    /**
     * Direction of the timeline.
     *
     * When the direction of the timeline is Forward, the progress
     * value will go from 0.0 to 1.0.
     *
     * When the direction of the timeline is Backward, the progress
     * value will go from 1.0 to 0.0.
     */
    enum Direction { Forward, Backward };

    /**
     * Constructs a new instance of TimeLine.
     *
     * @param duration Duration of the timeline, in milliseconds
     * @param direction Direction of the timeline
     * @since 5.14
     */
    explicit TimeLine(std::chrono::milliseconds duration = std::chrono::milliseconds(1000),
                      Direction direction = Forward);
    TimeLine(const TimeLine& other);
    ~TimeLine();

    /**
     * Returns the current value of the timeline.
     *
     * @since 5.14
     */
    qreal value() const;

    /**
     * Updates the progress of the timeline.
     *
     * @note The delta value should be a non-negative number, i.e. it
     * should be greater or equal to 0.
     *
     * @param delta The number milliseconds passed since last frame
     * @since 5.14
     */
    void update(std::chrono::milliseconds delta);

    /**
     * Returns the number of elapsed milliseconds.
     *
     * @see setElapsed
     * @since 5.14
     */
    std::chrono::milliseconds elapsed() const;

    /**
     * Sets the number of elapsed milliseconds.
     *
     * This method overwrites previous value of elapsed milliseconds.
     * If the new value of elapsed milliseconds is greater or equal
     * to duration of the timeline, the timeline will be finished, i.e.
     * proceeding TimeLine::done method calls will return @c true.
     * Please don't use it. Instead, use TimeLine::update.
     *
     * @note The new number of elapsed milliseconds should be a non-negative
     * number, i.e. it should be greater or equal to 0.
     *
     * @param elapsed The new number of elapsed milliseconds
     * @see elapsed
     * @since 5.14
     */
    void setElapsed(std::chrono::milliseconds elapsed);

    /**
     * Returns the duration of the timeline.
     *
     * @returns Duration of the timeline, in milliseconds
     * @see setDuration
     * @since 5.14
     */
    std::chrono::milliseconds duration() const;

    /**
     * Sets the duration of the timeline.
     *
     * In addition to setting new value of duration, the timeline will
     * try to retarget the number of elapsed milliseconds to match
     * as close as possible old progress value. If the new duration
     * is much smaller than old duration, there is a big chance that
     * the timeline will be finished after setting new duration.
     *
     * @note The new duration should be a positive number, i.e. it
     * should be greater or equal to 1.
     *
     * @param duration The new duration of the timeline, in milliseconds
     * @see duration
     * @since 5.14
     */
    void setDuration(std::chrono::milliseconds duration);

    /**
     * Returns the direction of the timeline.
     *
     * @returns Direction of the timeline(TimeLine::Forward or TimeLine::Backward)
     * @see setDirection
     * @see toggleDirection
     * @since 5.14
     */
    Direction direction() const;

    /**
     * Sets the direction of the timeline.
     *
     * @param direction The new direction of the timeline
     * @see direction
     * @see toggleDirection
     * @since 5.14
     */
    void setDirection(Direction direction);

    /**
     * Toggles the direction of the timeline.
     *
     * If the direction of the timeline was TimeLine::Forward, it becomes
     * TimeLine::Backward, and vice verca.
     *
     * @see direction
     * @see setDirection
     * @since 5.14
     */
    void toggleDirection();

    /**
     * Returns the easing curve of the timeline.
     *
     * @see setEasingCurve
     * @since 5.14
     */
    QEasingCurve easingCurve() const;

    /**
     * Sets new easing curve.
     *
     * @param easingCurve An easing curve to be set
     * @see easingCurve
     * @since 5.14
     */
    void setEasingCurve(const QEasingCurve& easingCurve);

    /**
     * Sets new easing curve by providing its type.
     *
     * @param type Type of the easing curve(e.g. QEasingCurve::InCubic, etc)
     * @see easingCurve
     * @since 5.14
     */
    void setEasingCurve(QEasingCurve::Type type);

    /**
     * Returns whether the timeline is currently in progress.
     *
     * @see done
     * @since 5.14
     */
    bool running() const;

    /**
     * Returns whether the timeline is finished.
     *
     * @see reset
     * @since 5.14
     */
    bool done() const;

    /**
     * Resets the timeline to initial state.
     *
     * @since 5.14
     */
    void reset();

    enum class RedirectMode { Strict, Relaxed };

    /**
     * Returns the redirect mode for the source position.
     *
     * The redirect mode controls behavior of the timeline when its direction is
     * changed at the source position, e.g. what should we do when the timeline
     * initially goes forward and we change its direction to go backward.
     *
     * In the strict mode, the timeline will stop.
     *
     * In the relaxed mode, the timeline will go in the new direction. For example,
     * if the timeline goes forward(from 0 to 1), then with the new direction it
     * will go backward(from 1 to 0).
     *
     * The default is RedirectMode::Relaxed.
     *
     * @see targetRedirectMode
     * @since 5.15
     */
    RedirectMode sourceRedirectMode() const;

    /**
     * Sets the redirect mode for the source position.
     *
     * @param mode The new mode.
     * @since 5.15
     */
    void setSourceRedirectMode(RedirectMode mode);

    /**
     * Returns the redirect mode for the target position.
     *
     * The redirect mode controls behavior of the timeline when its direction is
     * changed at the target position.
     *
     * In the strict mode, subsequent update calls won't have any effect on the
     * current value of the timeline.
     *
     * In the relaxed mode, the timeline will go in the new direction.
     *
     * The default is RedirectMode::Strict.
     *
     * @see sourceRedirectMode
     * @since 5.15
     */
    RedirectMode targetRedirectMode() const;

    /**
     * Sets the redirect mode for the target position.
     *
     * @param mode The new mode.
     * @since 5.15
     */
    void setTargetRedirectMode(RedirectMode mode);

    TimeLine& operator=(const TimeLine& other);

private:
    qreal progress() const;

private:
    class Data;
    QSharedDataPointer<Data> d;
};

/***************************************************************
 WindowVertex
***************************************************************/

inline WindowVertex::WindowVertex()
    : px(0)
    , py(0)
    , ox(0)
    , oy(0)
    , tx(0)
    , ty(0)
{
}

inline WindowVertex::WindowVertex(double _x, double _y, double _tx, double _ty)
    : px(_x)
    , py(_y)
    , ox(_x)
    , oy(_y)
    , tx(_tx)
    , ty(_ty)
{
}

inline WindowVertex::WindowVertex(const QPointF& position, const QPointF& texturePosition)
    : px(position.x())
    , py(position.y())
    , ox(position.x())
    , oy(position.y())
    , tx(texturePosition.x())
    , ty(texturePosition.y())
{
}

inline void WindowVertex::move(double x, double y)
{
    px = x;
    py = y;
}

inline void WindowVertex::setX(double x)
{
    px = x;
}

inline void WindowVertex::setY(double y)
{
    py = y;
}

/***************************************************************
 WindowQuad
***************************************************************/

inline WindowQuad::WindowQuad(WindowQuadType t, int id)
    : quadType(t)
    , uvSwapped(false)
    , quadID(id)
{
}

inline WindowVertex& WindowQuad::operator[](int index)
{
    Q_ASSERT(index >= 0 && index < 4);
    return verts[index];
}

inline const WindowVertex& WindowQuad::operator[](int index) const
{
    Q_ASSERT(index >= 0 && index < 4);
    return verts[index];
}

inline WindowQuadType WindowQuad::type() const
{
    Q_ASSERT(quadType != WindowQuadError);
    return quadType;
}

inline int WindowQuad::id() const
{
    return quadID;
}

inline bool WindowQuad::decoration() const
{
    Q_ASSERT(quadType != WindowQuadError);
    return quadType == WindowQuadDecoration;
}

inline bool WindowQuad::effect() const
{
    Q_ASSERT(quadType != WindowQuadError);
    return quadType >= EFFECT_QUAD_TYPE_START;
}

inline bool WindowQuad::isTransformed() const
{
    return !(verts[0].px == verts[0].ox && verts[0].py == verts[0].oy && verts[1].px == verts[1].ox
             && verts[1].py == verts[1].oy && verts[2].px == verts[2].ox
             && verts[2].py == verts[2].oy && verts[3].px == verts[3].ox
             && verts[3].py == verts[3].oy);
}

inline double WindowQuad::left() const
{
    return qMin(verts[0].px, qMin(verts[1].px, qMin(verts[2].px, verts[3].px)));
}

inline double WindowQuad::right() const
{
    return qMax(verts[0].px, qMax(verts[1].px, qMax(verts[2].px, verts[3].px)));
}

inline double WindowQuad::top() const
{
    return qMin(verts[0].py, qMin(verts[1].py, qMin(verts[2].py, verts[3].py)));
}

inline double WindowQuad::bottom() const
{
    return qMax(verts[0].py, qMax(verts[1].py, qMax(verts[2].py, verts[3].py)));
}

inline double WindowQuad::originalLeft() const
{
    return verts[0].ox;
}

inline double WindowQuad::originalRight() const
{
    return verts[2].ox;
}

inline double WindowQuad::originalTop() const
{
    return verts[0].oy;
}

inline double WindowQuad::originalBottom() const
{
    return verts[2].oy;
}

/***************************************************************
 Motion
***************************************************************/

template<typename T>
Motion<T>::Motion(T initial, double strength, double smoothness)
    : m_value(initial)
    , m_start(initial)
    , m_target(initial)
    , m_velocity()
    , m_strength(strength)
    , m_smoothness(smoothness)
{
}

template<typename T>
Motion<T>::Motion(const Motion& other)
    : m_value(other.value())
    , m_start(other.target())
    , m_target(other.target())
    , m_velocity(other.velocity())
    , m_strength(other.strength())
    , m_smoothness(other.smoothness())
{
}

template<typename T>
Motion<T>::~Motion()
{
}

template<typename T>
void Motion<T>::calculate(const int msec)
{
    if (m_value == m_target && m_velocity == T()) // At target and not moving
        return;

    // Poor man's time independent calculation
    int steps = qMax(1, msec / 5);
    for (int i = 0; i < steps; i++) {
        T diff = m_target - m_value;
        T strength = diff * m_strength;
        m_velocity = (m_smoothness * m_velocity + strength) / (m_smoothness + 1.0);
        m_value += m_velocity;
    }
}

template<typename T>
void Motion<T>::finish()
{
    m_value = m_target;
    m_velocity = T();
}

} // namespace
Q_DECLARE_METATYPE(KWin::EffectWindow*)
Q_DECLARE_METATYPE(KWin::EffectWindowList)
Q_DECLARE_METATYPE(KWin::TimeLine)
Q_DECLARE_METATYPE(KWin::TimeLine::Direction)

/** @} */

#endif // KWINEFFECTS_H
