/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

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
#pragma once

#include "scene.h"
#include "win/types.h"

#include <kwineffects/effect_frame.h>
#include <kwineffects/effect_screen.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>

#include <Plasma/FrameSvg>
#include <QHash>
#include <QMouseEvent>
#include <memory>
#include <set>

namespace Wrapland::Server
{
class Display;
}

namespace KWin
{

namespace base
{
class output;
}

namespace win
{
class space;
}

class Toplevel;

namespace render
{

namespace x11
{
template<typename Effects, typename Space>
class property_notify_filter;
}

class effect_loader;
class compositor;

class KWIN_EXPORT effects_handler_impl : public EffectsHandler
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")
    Q_PROPERTY(QStringList activeEffects READ activeEffects)
    Q_PROPERTY(QStringList loadedEffects READ loadedEffects)
    Q_PROPERTY(QStringList listOfEffects READ listOfEffects)
public:
    effects_handler_impl(render::compositor* compositor, render::scene* scene);
    ~effects_handler_impl() override;
    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion& region, ScreenPaintData& data) override;
    /**
     * Special hook to perform a paintScreen but just with the windows on @p desktop.
     */
    void paintDesktop(int desktop, int mask, QRegion region, ScreenPaintData& data);
    void postPaintScreen() override;
    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void
    paintWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;
    void postPaintWindow(EffectWindow* w) override;

    Effect* provides(Effect::Feature ef);

    void
    drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;

    void buildQuads(EffectWindow* w, WindowQuadList& quadList) override;

    void activateWindow(EffectWindow* c) override;
    EffectWindow* activeWindow() const override;
    void moveWindow(EffectWindow* w,
                    const QPoint& pos,
                    bool snap = false,
                    double snapAdjust = 1.0) override;
    void windowToDesktop(EffectWindow* w, int desktop) override;
    void windowToScreen(EffectWindow* w, int screen) override;
    void setShowingDesktop(bool showing) override;

    QString currentActivity() const override;
    int currentDesktop() const override;
    int numberOfDesktops() const override;
    void setCurrentDesktop(int desktop) override;
    void setNumberOfDesktops(int desktops) override;
    QSize desktopGridSize() const override;
    int desktopGridWidth() const override;
    int desktopGridHeight() const override;
    int workspaceWidth() const override;
    int workspaceHeight() const override;
    int desktopAtCoords(QPoint coords) const override;
    QPoint desktopGridCoords(int id) const override;
    QPoint desktopCoords(int id) const override;
    int desktopAbove(int desktop = 0, bool wrap = true) const override;
    int desktopToRight(int desktop = 0, bool wrap = true) const override;
    int desktopBelow(int desktop = 0, bool wrap = true) const override;
    int desktopToLeft(int desktop = 0, bool wrap = true) const override;
    QString desktopName(int desktop) const override;
    bool optionRollOverDesktops() const override;

    QPoint cursorPos() const override;
    bool grabKeyboard(Effect* effect) override;
    void ungrabKeyboard() override;
    // not performing XGrabPointer
    void startMouseInterception(Effect* effect, Qt::CursorShape shape) override;
    void stopMouseInterception(Effect* effect) override;
    bool isMouseInterception() const;
    void registerGlobalShortcut(const QKeySequence& shortcut, QAction* action) override;
    void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButton pointerButtons,
                                 QAction* action) override;
    void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis,
                              QAction* action) override;
    void registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action) override;
    void* getProxy(QString name) override;
    void startMousePolling() override;
    void stopMousePolling() override;

    EffectWindowList stackingOrder() const override;
    void setElevatedWindow(KWin::EffectWindow* w, bool set) override;

    void setTabBoxWindow(EffectWindow*) override;
    void setTabBoxDesktop(int) override;
    EffectWindowList currentTabBoxWindowList() const override;
    void refTabBox() override;
    void unrefTabBox() override;
    void closeTabBox() override;
    QList<int> currentTabBoxDesktopList() const override;
    int currentTabBoxDesktop() const override;
    EffectWindow* currentTabBoxWindow() const override;

    void setActiveFullScreenEffect(Effect* e) override;
    Effect* activeFullScreenEffect() const override;
    bool hasActiveFullScreenEffect() const override;

    void addRepaintFull() override;
    void addRepaint(const QRect& r) override;
    void addRepaint(const QRegion& r) override;
    void addRepaint(int x, int y, int w, int h) override;
    int activeScreen() const override;
    int numScreens() const override;
    int screenNumber(const QPoint& pos) const override;
    QRect clientArea(clientAreaOption, int screen, int desktop) const override;
    QRect clientArea(clientAreaOption, const EffectWindow* c) const override;
    QRect clientArea(clientAreaOption, const QPoint& p, int desktop) const override;
    QSize virtualScreenSize() const override;
    QRect virtualScreenGeometry() const override;
    double animationTimeFactor() const override;
    WindowQuadType newWindowQuadType() override;

    void defineCursor(Qt::CursorShape shape) override;
    bool checkInputWindowEvent(QMouseEvent* e);
    bool checkInputWindowEvent(QWheelEvent* e);
    void checkInputWindowStacking();

    void reserveElectricBorder(ElectricBorder border, Effect* effect) override;
    void unreserveElectricBorder(ElectricBorder border, Effect* effect) override;

    void registerTouchBorder(ElectricBorder border, QAction* action) override;
    void unregisterTouchBorder(ElectricBorder border, QAction* action) override;

    unsigned long xrenderBufferPicture() const override;
    QPainter* scenePainter() override;
    void reconfigure() override;
    QByteArray readRootProperty(long atom, long type, int format) const override;
    xcb_atom_t announceSupportProperty(const QByteArray& propertyName, Effect* effect) override;
    void removeSupportProperty(const QByteArray& propertyName, Effect* effect) override;

    bool hasDecorationShadows() const override;
    bool decorationsHaveAlpha() const override;

    EffectFrame* effectFrame(EffectFrameStyle style,
                             bool staticSize,
                             const QPoint& position,
                             Qt::Alignment alignment) const override;

    QVariant kwinOption(KWinOption kwopt) override;
    bool isScreenLocked() const override;

    bool makeOpenGLContextCurrent() override;
    void doneOpenGLContextCurrent() override;

    xcb_connection_t* xcbConnection() const override;
    xcb_window_t x11RootWindow() const override;

    // internal (used by kwin core or compositing code)
    void startPaint();
    void grabbedKeyboardEvent(QKeyEvent* e);
    bool hasKeyboardGrab() const;
    void desktopResized(const QSize& size);

    void reloadEffect(Effect* effect) override;
    QStringList loadedEffects() const;
    QStringList listOfEffects() const;
    void unloadAllEffects();

    QList<EffectWindow*> elevatedWindows() const;
    QStringList activeEffects() const;

    /**
     * @returns Whether we are currently in a desktop rendering process triggered by paintDesktop
     * hook
     */
    bool isDesktopRendering() const
    {
        return m_desktopRendering;
    }
    /**
     * @returns the desktop currently being rendered in the paintDesktop hook.
     */
    int currentRenderedDesktop() const
    {
        return m_currentRenderedDesktop;
    }

    Wrapland::Server::Display* waylandDisplay() const override;

    bool animationsSupported() const override;

    PlatformCursorImage cursorImage() const override;

    void hideCursor() override;
    void showCursor() override;

    void
    startInteractiveWindowSelection(std::function<void(KWin::EffectWindow*)> callback) override;
    void startInteractivePositionSelection(std::function<void(const QPoint&)> callback) override;

    void showOnScreenMessage(const QString& message, const QString& iconName = QString()) override;
    void hideOnScreenMessage(OnScreenMessageHideFlags flags = OnScreenMessageHideFlags()) override;

    KSharedConfigPtr config() const override;
    KSharedConfigPtr inputConfig() const override;

    render::scene* scene() const
    {
        return m_scene;
    }

    bool touchDown(qint32 id, const QPointF& pos, quint32 time);
    bool touchMotion(qint32 id, const QPointF& pos, quint32 time);
    bool touchUp(qint32 id, quint32 time);

    void highlightWindows(const QVector<EffectWindow*>& windows);
    void windowToDesktops(EffectWindow* w, const QVector<uint>& desktops) override;

    /**
     * Finds an effect with the given name.
     *
     * @param name The name of the effect.
     * @returns The effect with the given name @p name, or nullptr if there
     *     is no such effect loaded.
     */
    Effect* findEffect(const QString& name) const;

    void renderEffectQuickView(EffectQuickView* effectQuickView) const override;

    SessionState sessionState() const override;
    QList<EffectScreen*> screens() const override;
    EffectScreen* screenAt(const QPoint& point) const override;
    EffectScreen* findScreen(const QString& name) const override;
    EffectScreen* findScreen(int screenId) const override;
    bool isCursorHidden() const override;
    QImage blit_from_framebuffer(QRect const& geometry, double scale) const override;
    bool invert_screen();

    QRect renderTargetRect() const override;
    qreal renderTargetScale() const override;

    using PropertyEffectMap = QHash<QByteArray, QList<Effect*>>;
    PropertyEffectMap m_propertiesForEffects;
    QHash<QByteArray, qulonglong> m_managedProperties;
    render::compositor* m_compositor;
    QHash<long, int> registered_atoms;

public Q_SLOTS:
    void slotCurrentTabAboutToChange(EffectWindow* from, EffectWindow* to);
    void slotTabAdded(EffectWindow* from, EffectWindow* to);
    void slotTabRemoved(EffectWindow* c, EffectWindow* newActiveWindow);

    // slots for D-Bus interface
    Q_SCRIPTABLE void reconfigureEffect(const QString& name);
    Q_SCRIPTABLE bool loadEffect(const QString& name);
    Q_SCRIPTABLE void toggleEffect(const QString& name);
    Q_SCRIPTABLE void unloadEffect(const QString& name);
    Q_SCRIPTABLE bool isEffectLoaded(const QString& name) const override;
    Q_SCRIPTABLE bool isEffectSupported(const QString& name);
    Q_SCRIPTABLE QList<bool> areEffectsSupported(const QStringList& names);
    Q_SCRIPTABLE QString supportInformation(const QString& name) const;
    Q_SCRIPTABLE QString debug(const QString& name, const QString& parameter = QString()) const;

protected Q_SLOTS:
    void slotClientShown(KWin::Toplevel*);
    void slotXdgShellClientShown(KWin::Toplevel*);
    void slotUnmanagedShown(KWin::Toplevel*);
    void slotClientMaximized(KWin::Toplevel* window, win::maximize_mode maxMode);
    void slotOpacityChanged(KWin::Toplevel* t, qreal oldOpacity);
    void slotClientModalityChanged(KWin::Toplevel* window);
    void slotGeometryShapeChanged(KWin::Toplevel* t, const QRect& old);
    void slotFrameGeometryChanged(Toplevel* toplevel, const QRect& oldGeometry);
    void slotPaddingChanged(KWin::Toplevel* t, const QRect& old);
    void slotWindowDamaged(KWin::Toplevel* t, const QRegion& r);
    void slotOutputEnabled(base::output* output);
    void slotOutputDisabled(base::output* output);

protected:
    void connectNotify(const QMetaMethod& signal) override;
    void disconnectNotify(const QMetaMethod& signal) override;
    void effectsChanged();
    void setupAbstractClientConnections(Toplevel* window);

    // For X11 windows
    void setupClientConnections(Toplevel* c);
    void setupUnmanagedConnections(Toplevel* u);

    EffectWindow* find_window_by_wid(WId id) const override;
    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* surface) const override;
    EffectWindow* find_window_by_qwindow(QWindow* w) const override;
    EffectWindow* find_window_by_uuid(const QUuid& id) const override;

    /**
     * Default implementation does nothing and returns @c true.
     */
    virtual bool doGrabKeyboard();
    /**
     * Default implementation does nothing.
     */
    virtual void doUngrabKeyboard();

    /**
     * Default implementation sets Effects override cursor on the input::pointer_redirect.
     */
    virtual void doStartMouseInterception(Qt::CursorShape shape);

    /**
     * Default implementation removes the Effects override cursor on the input::pointer_redirect.
     */
    virtual void doStopMouseInterception();

    /**
     * Default implementation does nothing
     */
    virtual void doCheckInputWindowStacking();

    virtual void handle_effect_destroy(Effect& effect) = 0;
    void unreserve_borders(Effect& effect);

    Effect* keyboard_grab_effect{nullptr};
    Effect* fullscreen_effect{nullptr};
    QList<EffectWindow*> elevated_windows;
    QMultiMap<int, EffectPair> effect_order;
    int next_window_quad_type{EFFECT_QUAD_TYPE_START};

private:
    void add_remnant(Toplevel* remnant);
    void destroyEffect(Effect* effect);

    typedef QVector<Effect*> EffectsList;
    typedef EffectsList::const_iterator EffectsIterator;
    EffectsList m_activeEffects;
    EffectsIterator m_currentDrawWindowIterator;
    EffectsIterator m_currentPaintWindowIterator;
    EffectsIterator m_currentPaintScreenIterator;
    EffectsIterator m_currentBuildQuadsIterator;
    render::scene* m_scene;
    bool m_desktopRendering{false};
    int m_currentRenderedDesktop{0};
    QList<Effect*> m_grabbedMouseEffects;
    effect_loader* m_effectLoader;
    int m_trackingCursorChanges{0};
    std::unique_ptr<x11::property_notify_filter<effects_handler_impl, win::space>>
        x11_property_notify;
    QList<EffectScreen*> m_effectScreens;
    std::unordered_map<Effect*, std::unordered_map<ElectricBorder, uint32_t>> reserved_borders;
};

}
}
