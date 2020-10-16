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

// kwin
#include "input.h"
#include "utils.h"
#include "virtualdesktops.h"
#include "xcbutils.h"
// KDE
#include <NETWM>
// Qt
#include <QObject>
#include <QMatrix4x4>
#include <QUuid>
// xcb
#include <xcb/damage.h>
#include <xcb/xfixes.h>
// c++
#include <functional>

class QOpenGLFramebufferObject;

namespace Wrapland
{
namespace Server
{
class Surface;
}
}

namespace KWin
{

class ClientMachine;
class Deleted;
class EffectWindowImpl;
class Shadow;

/**
 * Enum to describe the reason why a Toplevel has to be released.
 */
enum class ReleaseReason {
    Release, ///< Normal Release after e.g. an Unmap notify event (window still valid)
    Destroyed, ///< Release after an Destroy notify event (window no longer valid)
    KWinShutsDown ///< Release on KWin Shutdown (window still valid)
};

class KWIN_EXPORT Toplevel : public QObject
{
    Q_OBJECT

public:
    explicit Toplevel();
    virtual xcb_window_t frameId() const;
    xcb_window_t window() const;
    /**
     * @return a unique identifier for the Toplevel. On X11 same as @ref window
     */
    virtual quint32 windowId() const;
    /**
     * Returns the geometry of the pixmap or buffer attached to this Toplevel.
     *
     * For X11 clients, this method returns server-side geometry of the Toplevel.
     *
     * For Wayland clients, this method returns rectangle that the main surface
     * occupies on the screen, in global screen coordinates.
     */
    virtual QRect bufferGeometry() const = 0;
    /**
     * Returns the extents of invisible portions in the pixmap.
     *
     * An X11 pixmap may contain invisible space around the actual contents of the
     * client. That space is reserved for server-side decoration, which we usually
     * want to skip when building contents window quads.
     *
     * Default implementation returns a margins object with all margins set to 0.
     */
    virtual QMargins bufferMargins() const;
    /**
     * Returns the geometry of the Toplevel, excluding invisible portions, e.g.
     * server-side and client-side drop shadows, etc.
     */
    QRect frameGeometry() const;
    /**
     * Returns the extents of the server-side decoration.
     *
     * Note that the returned margins object will have all margins set to 0 if
     * the client doesn't have a server-side decoration.
     *
     * Default implementation returns a margins object with all margins set to 0.
     */
    virtual QMargins frameMargins() const;
    /**
     * The geometry of the Toplevel which accepts input events. This might be larger
     * than the actual geometry, e.g. to support resizing outside the window.
     *
     * Default implementation returns same as geometry.
     */
    virtual QRect inputGeometry() const;
    QSize size() const;
    QPoint pos() const;
    QRect rect() const;
    int x() const;
    int y() const;
    int width() const;
    int height() const;
    bool isOnScreen(int screen) const;   // true if it's at least partially there
    bool isOnActiveScreen() const;
    int screen() const; // the screen where the center is
    /**
     * The scale of the screen this window is currently on
     * @note The buffer scale can be different.
     * @since 5.12
     */
    qreal screenScale() const; //
    /**
     * Returns the ratio between physical pixels and device-independent pixels for
     * the attached buffer (or pixmap).
     *
     * For X11 clients, this method always returns 1.
     */
    virtual qreal bufferScale() const;
    virtual QPoint clientPos() const = 0; // inside of geometry()
    /**
     * Describes how the client's content maps to the window geometry including the frame.
     * The default implementation is a 1:1 mapping meaning the frame is part of the content.
     */
    virtual QPoint clientContentPos() const;
    virtual QSize clientSize() const = 0;
    virtual QRect visibleRect() const; // the area the window occupies on the screen
    virtual QRect decorationRect() const; // rect including the decoration shadows
    virtual QRect transparentRect() const = 0;
    virtual bool isClient() const;
    virtual bool isDeleted() const;

    // prefer isXXX() instead
    // 0 for supported types means default for managed/unmanaged types
    virtual NET::WindowType windowType(bool direct = false, int supported_types = 0) const = 0;
    bool hasNETSupport() const;
    bool isDesktop() const;
    bool isDock() const;
    bool isToolbar() const;
    bool isMenu() const;
    bool isNormalWindow() const; // normal as in 'NET::Normal or NET::Unknown non-transient'
    bool isDialog() const;
    bool isSplash() const;
    bool isUtility() const;
    bool isDropdownMenu() const;
    bool isPopupMenu() const; // a context popup, not dropdown, not torn-off
    bool isTooltip() const;
    bool isNotification() const;
    bool isCriticalNotification() const;
    bool isOnScreenDisplay() const;
    bool isComboBox() const;
    bool isDNDIcon() const;

    virtual bool isLockScreen() const;
    virtual bool isInputMethod() const;
    virtual bool isOutline() const;

    /**
     * Returns the virtual desktop within the workspace() the client window
     * is located in, 0 if it isn't located on any special desktop (not mapped yet),
     * or NET::OnAllDesktops. Do not use desktop() directly, use
     * isOnDesktop() instead.
     */
    virtual int desktop() const = 0;
    virtual QVector<VirtualDesktop *> desktops() const = 0;
    virtual QStringList activities() const = 0;
    bool isOnDesktop(int d) const;
    bool isOnActivity(const QString &activity) const;
    bool isOnCurrentDesktop() const;
    bool isOnCurrentActivity() const;
    bool isOnAllDesktops() const;
    bool isOnAllActivities() const;

    virtual QByteArray windowRole() const;
    QByteArray sessionId() const;
    QByteArray resourceName() const;
    QByteArray resourceClass() const;
    QByteArray wmCommand();
    QByteArray wmClientMachine(bool use_localhost) const;
    const ClientMachine *clientMachine() const;
    virtual bool isLocalhost() const;
    xcb_window_t wmClientLeader() const;
    virtual pid_t pid() const;
    static bool resourceMatch(const Toplevel* c1, const Toplevel* c2);

    bool readyForPainting() const; // true if the window has been already painted its contents
    xcb_visualid_t visual() const;
    bool shape() const;
    QRegion inputShape() const;
    virtual void setOpacity(double opacity);
    virtual double opacity() const;
    int depth() const;
    bool hasAlpha() const;
    virtual bool setupCompositing();
    virtual void finishCompositing(ReleaseReason releaseReason = ReleaseReason::Release);
    Q_INVOKABLE void addRepaint(const QRect& r);
    Q_INVOKABLE void addRepaint(const QRegion& r);
    Q_INVOKABLE void addRepaint(int x, int y, int w, int h);
    Q_INVOKABLE void addLayerRepaint(const QRect& r);
    Q_INVOKABLE void addLayerRepaint(const QRegion& r);
    Q_INVOKABLE void addLayerRepaint(int x, int y, int w, int h);
    Q_INVOKABLE virtual void addRepaintFull();
    // these call workspace->addRepaint(), but first transform the damage if needed
    void addWorkspaceRepaint(const QRect& r);
    void addWorkspaceRepaint(int x, int y, int w, int h);
    QRegion repaints() const;
    void resetRepaints();
    QRegion damage() const;
    void resetDamage();
    EffectWindowImpl* effectWindow();
    const EffectWindowImpl* effectWindow() const;

    /**
     * Returns the pointer to the Toplevel's Shadow. A Shadow
     * is only available if Compositing is enabled and the corresponding X window
     * has the Shadow property set.
     * @returns The Shadow belonging to this Toplevel, @c null if there's no Shadow.
     */
    const Shadow *shadow() const;
    Shadow *shadow();
    /**
     * Updates the Shadow associated with this Toplevel from X11 Property.
     * Call this method when the Property changes or Compositing is started.
     */
    void updateShadow();
    /**
     * Whether the Toplevel currently wants the shadow to be rendered. Default
     * implementation always returns @c true.
     */
    virtual bool wantsShadowToBeRendered() const;

    /**
     * This method returns the area that the Toplevel window reports to be opaque.
     * It is supposed to only provide valuable information if hasAlpha is @c true .
     * @see hasAlpha
     */
    const QRegion& opaqueRegion() const;

    virtual Layer layer() const = 0;

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

    quint32 surfaceId() const;
    Wrapland::Server::Surface *surface() const;
    void setSurface(Wrapland::Server::Surface *surface);

    const QSharedPointer<QOpenGLFramebufferObject> &internalFramebufferObject() const;
    QImage internalImageObject() const;

    /**
     * @returns Transformation to map from global to window coordinates.
     *
     * Default implementation returns a translation on negative pos().
     * @see pos
     */
    virtual QMatrix4x4 inputTransformation() const;

    /**
     * The window has a popup grab. This means that when it got mapped the
     * parent window had an implicit (pointer) grab.
     *
     * Normally this is only relevant for transient windows.
     *
     * Once the popup grab ends (e.g. pointer press outside of any Toplevel of
     * the client), the method popupDone should be invoked.
     *
     * The default implementation returns @c false.
     * @see popupDone
     * @since 5.10
     */
    virtual bool hasPopupGrab() const {
        return false;
    }
    /**
     * This method should be invoked for Toplevels with a popup grab when
     * the grab ends.
     *
     * The default implementation does nothing.
     * @see hasPopupGrab
     * @since 5.10
     */
    virtual void popupDone() {};

    /**
     * Whether the window is a popup.
     *
     * Popups can be used to implement popup menus, tooltips, combo boxes, etc.
     *
     * @since 5.15
     */
    virtual bool isPopupWindow() const;

    /**
     * A UUID to uniquely identify this Toplevel independent of windowing system.
     */
    QUuid internalId() const
    {
        return m_internalId;
    }

Q_SIGNALS:
    void opacityChanged(KWin::Toplevel* toplevel, qreal oldOpacity);
    void damaged(KWin::Toplevel* toplevel, const QRect& damage);
    void geometryChanged();
    void frameGeometryChanged(KWin::Toplevel* toplevel, const QRect& old);
    void geometryShapeChanged(KWin::Toplevel* toplevel, const QRect& old);
    void paddingChanged(KWin::Toplevel* toplevel, const QRect& old);
    void windowClosed(KWin::Toplevel* toplevel, KWin::Deleted* deleted);
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
    void activitiesChanged(KWin::Toplevel* toplevel);
    /**
     * Emitted whenever the Toplevel's screen changes. This can happen either in consequence to
     * a screen being removed/added or if the Toplevel's geometry changes.
     * @since 4.11
     */
    void screenChanged();
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

    /*
     * Emitted when the client's screen changes onto a screen of a different scale
     * or the screen we're on changes
     * @since 5.12
     */
    void screenScaleChanged();

    /**
     * Emitted whenever the client's shadow changes.
     * @since 5.15
     */
    void shadowChanged();

public Q_SLOTS:
    /**
     * Checks whether the screen number for this Toplevel changed and updates if needed.
     * Any method changing the geometry of the Toplevel should call this method.
     */
    void checkScreen();

protected Q_SLOTS:
    void setupCheckScreenConnection();
    void removeCheckScreenConnection();
    void setReadyForPainting();

protected:
    ~Toplevel() override;
    void setWindowHandles(xcb_window_t client);
    void detectShape(xcb_window_t id);
    virtual void propertyNotifyEvent(xcb_property_notify_event_t *e);
    virtual void damageNotifyEvent();
    virtual void clientMessageEvent(xcb_client_message_event_t *e);
    void discardWindowPixmap();
    void addDamageFull();
    virtual void addDamage(const QRegion &damage);
    Xcb::Property fetchWmClientLeader() const;
    void readWmClientLeader(Xcb::Property &p);
    void getWmClientLeader();
    void getWmClientMachine();

    /**
     * This function fetches the opaque region from this Toplevel.
     * Will only be called on corresponding property changes and for initialization.
     */
    void getWmOpaqueRegion();

    void getResourceClass();
    void setResourceClass(const QByteArray &name, const QByteArray &className = QByteArray());
    void getSkipCloseAnimation();
    virtual void debug(QDebug& stream) const = 0;
    void copyToDeleted(Toplevel* c);
    void disownDataPassedToDeleted();
    friend QDebug& operator<<(QDebug& stream, const Toplevel*);
    void deleteEffectWindow();
    void setDepth(int depth);
    QRect m_frameGeometry;
    xcb_visualid_t m_visual;
    int bit_depth;
    NETWinInfo* info;
    bool ready_for_painting;
    QRegion repaints_region; // updating, repaint just requires repaint of that area
    QRegion layer_repaints_region;
    /**
     * An FBO object KWin internal windows might render to.
     */
    QSharedPointer<QOpenGLFramebufferObject> m_internalFBO;
    QImage m_internalImage;

protected:
    bool m_isDamaged;

private:
    void handleXwaylandSurfaceSizeChange();
    void updateClientOutputs();
    // when adding new data members, check also copyToDeleted()
    QUuid m_internalId;
    Xcb::Window m_client;
    xcb_damage_damage_t damage_handle;
    QRegion damage_region; // damage is really damaged window (XDamage) and texture needs
    bool is_shape;
    EffectWindowImpl* effect_window;
    QByteArray resource_name;
    QByteArray resource_class;
    ClientMachine *m_clientMachine;
    xcb_window_t m_wmClientLeader;
    bool m_damageReplyPending;
    QRegion opaque_region;
    xcb_xfixes_fetch_region_cookie_t m_regionCookie;
    int m_screen;
    bool m_skipCloseAnimation;
    quint32 m_surfaceId = 0;
    Wrapland::Server::Surface *m_surface = nullptr;
    // when adding new data members, check also copyToDeleted()
    qreal m_screenScale = 1.0;
};

inline xcb_window_t Toplevel::window() const
{
    return m_client;
}

inline void Toplevel::setWindowHandles(xcb_window_t w)
{
    Q_ASSERT(!m_client.isValid() && w != XCB_WINDOW_NONE);
    m_client.reset(w, false);
}

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

inline int Toplevel::x() const
{
    return m_frameGeometry.x();
}

inline int Toplevel::y() const
{
    return m_frameGeometry.y();
}

inline int Toplevel::width() const
{
    return m_frameGeometry.width();
}

inline int Toplevel::height() const
{
    return m_frameGeometry.height();
}

inline QRect Toplevel::rect() const
{
    return QRect(0, 0, width(), height());
}

inline bool Toplevel::readyForPainting() const
{
    return ready_for_painting;
}

inline xcb_visualid_t Toplevel::visual() const
{
    return m_visual;
}

inline bool Toplevel::isDesktop() const
{
    return windowType() == NET::Desktop;
}

inline bool Toplevel::isDock() const
{
    return windowType() == NET::Dock;
}

inline bool Toplevel::isMenu() const
{
    return windowType() == NET::Menu;
}

inline bool Toplevel::isToolbar() const
{
    return windowType() == NET::Toolbar;
}

inline bool Toplevel::isSplash() const
{
    return windowType() == NET::Splash;
}

inline bool Toplevel::isUtility() const
{
    return windowType() == NET::Utility;
}

inline bool Toplevel::isDialog() const
{
    return windowType() == NET::Dialog;
}

inline bool Toplevel::isNormalWindow() const
{
    return windowType() == NET::Normal;
}

inline bool Toplevel::isDropdownMenu() const
{
    return windowType() == NET::DropdownMenu;
}

inline bool Toplevel::isPopupMenu() const
{
    return windowType() == NET::PopupMenu;
}

inline bool Toplevel::isTooltip() const
{
    return windowType() == NET::Tooltip;
}

inline bool Toplevel::isNotification() const
{
    return windowType() == NET::Notification;
}

inline bool Toplevel::isCriticalNotification() const
{
    return windowType() == NET::CriticalNotification;
}

inline bool Toplevel::isOnScreenDisplay() const
{
    return windowType() == NET::OnScreenDisplay;
}

inline bool Toplevel::isComboBox() const
{
    return windowType() == NET::ComboBox;
}

inline bool Toplevel::isDNDIcon() const
{
    return windowType() == NET::DNDIcon;
}

inline bool Toplevel::isLockScreen() const
{
    return false;
}

inline bool Toplevel::isInputMethod() const
{
    return false;
}

inline bool Toplevel::isOutline() const
{
    return false;
}

inline QRegion Toplevel::damage() const
{
    return damage_region;
}

inline QRegion Toplevel::repaints() const
{
    return repaints_region.translated(pos()) | layer_repaints_region;
}

inline bool Toplevel::shape() const
{
    return is_shape;
}

inline int Toplevel::depth() const
{
    return bit_depth;
}

inline bool Toplevel::hasAlpha() const
{
    return depth() == 32;
}

inline const QRegion& Toplevel::opaqueRegion() const
{
    return opaque_region;
}

inline
EffectWindowImpl* Toplevel::effectWindow()
{
    return effect_window;
}

inline
const EffectWindowImpl* Toplevel::effectWindow() const
{
    return effect_window;
}

inline QByteArray Toplevel::resourceName() const
{
    return resource_name; // it is always lowercase
}

inline QByteArray Toplevel::resourceClass() const
{
    return resource_class; // it is always lowercase
}

inline const ClientMachine *Toplevel::clientMachine() const
{
    return m_clientMachine;
}

inline quint32 Toplevel::surfaceId() const
{
    return m_surfaceId;
}

inline Wrapland::Server::Surface *Toplevel::surface() const
{
    return m_surface;
}

inline const QSharedPointer<QOpenGLFramebufferObject> &Toplevel::internalFramebufferObject() const
{
    return m_internalFBO;
}

inline QImage Toplevel::internalImageObject() const
{
    return m_internalImage;
}

inline QPoint Toplevel::clientContentPos() const
{
    return QPoint(0, 0);
}

QDebug& operator<<(QDebug& stream, const Toplevel*);

} // namespace
Q_DECLARE_METATYPE(KWin::Toplevel*)

#endif
