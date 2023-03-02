/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect_integration.h>
#include <kwineffects/export.h>
#include <kwineffects/types.h>
#include <kwinglobals.h>

#include <KSharedConfig>

class QAction;
class QKeyEvent;

namespace Wrapland::Server
{
class Surface;
class Display;
}

namespace KWin
{

class EffectFrame;
class EffectQuickView;
class EffectScreen;
class EffectWindow;
class ScreenPaintData;
class ScreenPrePaintData;
class WindowPaintData;
class WindowPrePaintData;
class WindowQuadList;

/**
 * @short Manager class that handles all the effects.
 *
 * This class creates Effect objects and calls it's appropriate methods.
 *
 * Effect objects can call methods of this class to interact with the
 *  workspace, e.g. to activate or move a specific window, change current
 *  desktop or create a special input window to receive mouse and keyboard
 *  events.
 */
class KWINEFFECTS_EXPORT EffectsHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int currentDesktop READ currentDesktop WRITE setCurrentDesktop NOTIFY desktopChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity NOTIFY currentActivityChanged)
    Q_PROPERTY(KWin::EffectWindow* activeWindow READ activeWindow WRITE activateWindow NOTIFY
                   windowActivated)
    Q_PROPERTY(QSize desktopGridSize READ desktopGridSize NOTIFY desktopGridSizeChanged)
    Q_PROPERTY(int desktopGridWidth READ desktopGridWidth NOTIFY desktopGridWidthChanged)
    Q_PROPERTY(int desktopGridHeight READ desktopGridHeight NOTIFY desktopGridHeightChanged)
    Q_PROPERTY(int workspaceWidth READ workspaceWidth)
    Q_PROPERTY(int workspaceHeight READ workspaceHeight)
    /**
     * The number of desktops currently used. Minimum number of desktops is 1, maximum 20.
     */
    Q_PROPERTY(
        int desktops READ numberOfDesktops WRITE setNumberOfDesktops NOTIFY numberDesktopsChanged)
    Q_PROPERTY(bool optionRollOverDesktops READ optionRollOverDesktops)
    Q_PROPERTY(KWin::EffectScreen* activeScreen READ activeScreen)
    /**
     * Factor by which animation speed in the effect should be modified (multiplied).
     * If configurable in the effect itself, the option should have also 'default'
     * animation speed. The actual value should be determined using animationTime().
     * Note: The factor can be also 0, so make sure your code can cope with 0ms time
     * if used manually.
     */
    Q_PROPERTY(qreal animationTimeFactor READ animationTimeFactor)
    Q_PROPERTY(KWin::EffectWindowList stackingOrder READ stackingOrder)
    /**
     * Whether window decorations use the alpha channel.
     */
    Q_PROPERTY(bool decorationsHaveAlpha READ decorationsHaveAlpha)
    Q_PROPERTY(CompositingType compositingType READ compositingType CONSTANT)
    Q_PROPERTY(QPoint cursorPos READ cursorPos)
    Q_PROPERTY(QSize virtualScreenSize READ virtualScreenSize NOTIFY virtualScreenSizeChanged)
    Q_PROPERTY(
        QRect virtualScreenGeometry READ virtualScreenGeometry NOTIFY virtualScreenGeometryChanged)
    Q_PROPERTY(bool hasActiveFullScreenEffect READ hasActiveFullScreenEffect NOTIFY
                   hasActiveFullScreenEffectChanged)

    /**
     * The status of the session i.e if the user is logging out
     * @since 5.18
     */
    Q_PROPERTY(KWin::SessionState sessionState READ sessionState NOTIFY sessionStateChanged)

    friend class Effect;

public:
    using TouchBorderCallback
        = std::function<void(ElectricBorder border, const QSizeF&, EffectScreen* screen)>;

    explicit EffectsHandler(CompositingType type);
    ~EffectsHandler() override;
    // for use by effects
    virtual void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
        = 0;
    virtual void paintScreen(int mask, const QRegion& region, ScreenPaintData& data) = 0;
    virtual void postPaintScreen() = 0;
    virtual void
    prePaintWindow(EffectWindow* w, WindowPrePaintData& data, std::chrono::milliseconds presentTime)
        = 0;
    virtual void
    paintWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data)
        = 0;
    virtual void postPaintWindow(EffectWindow* w) = 0;
    virtual void drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data)
        = 0;
    virtual void buildQuads(EffectWindow* w, WindowQuadList& quadList) = 0;
    virtual QVariant kwinOption(KWinOption kwopt) = 0;
    /**
     * Sets the cursor while the mouse is intercepted.
     * @see startMouseInterception
     * @since 4.11
     */
    virtual void defineCursor(Qt::CursorShape shape) = 0;
    virtual QPoint cursorPos() const = 0;
    virtual bool grabKeyboard(Effect* effect) = 0;
    virtual void ungrabKeyboard() = 0;
    /**
     * Ensures that all mouse events are sent to the @p effect.
     * No window will get the mouse events. Only fullscreen effects providing a custom user
     * interface should be using this method. The input events are delivered to
     * Effect::windowInputMouseEvent.
     *
     * @note This method does not perform an X11 mouse grab. On X11 a fullscreen input window is
     * raised above all other windows, but no grab is performed.
     *
     * @param effect The effect
     * @param shape Sets the cursor to be used while the mouse is intercepted
     * @see stopMouseInterception
     * @see Effect::windowInputMouseEvent
     * @since 4.11
     */
    virtual void startMouseInterception(Effect* effect, Qt::CursorShape shape) = 0;
    /**
     * Releases the hold mouse interception for @p effect
     * @see startMouseInterception
     * @since 4.11
     */
    virtual void stopMouseInterception(Effect* effect) = 0;

    /**
     * @brief Registers a global shortcut with the provided @p action.
     *
     * @param shortcut The global shortcut which should trigger the action
     * @param action The action which gets triggered when the shortcut matches
     */
    virtual QList<QKeySequence> registerGlobalShortcut(QList<QKeySequence> const& shortcut,
                                                       QAction* action)
        = 0;
    virtual QList<QKeySequence>
    registerGlobalShortcutAndDefault(QList<QKeySequence> const& shortcut, QAction* action) = 0;
    /**
     * @brief Registers a global pointer shortcut with the provided @p action.
     *
     * @param modifiers The keyboard modifiers which need to be holded
     * @param pointerButtons The pointer buttons which need to be pressed
     * @param action The action which gets triggered when the shortcut matches
     */
    virtual void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                         Qt::MouseButton pointerButtons,
                                         QAction* action)
        = 0;
    /**
     * @brief Registers a global axis shortcut with the provided @p action.
     *
     * @param modifiers The keyboard modifiers which need to be holded
     * @param axis The direction in which the axis needs to be moved
     * @param action The action which gets triggered when the shortcut matches
     */
    virtual void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                                      PointerAxisDirection axis,
                                      QAction* action)
        = 0;

    /**
     * @brief Registers a global touchpad swipe gesture shortcut with the provided @p action.
     *
     * @param direction The direction for the swipe
     * @param action The action which gets triggered when the gesture triggers
     * @since 5.10
     */
    virtual void
    registerTouchpadSwipeShortcut(SwipeDirection direction, uint fingerCount, QAction* action)
        = 0;

    virtual void registerRealtimeTouchpadSwipeShortcut(SwipeDirection dir,
                                                       uint fingerCount,
                                                       QAction* onUp,
                                                       std::function<void(qreal)> progressCallback)
        = 0;

    virtual void registerRealtimeTouchpadPinchShortcut(PinchDirection dir,
                                                       uint fingerCount,
                                                       QAction* onUp,
                                                       std::function<void(qreal)> progressCallback)
        = 0;

    virtual void
    registerTouchpadPinchShortcut(PinchDirection direction, uint fingerCount, QAction* action)
        = 0;

    /**
     * @brief Registers a global touchscreen swipe gesture shortcut with the provided @p action.
     *
     * @param direction The direction for the swipe
     * @param action The action which gets triggered when the gesture triggers
     * @since 5.25
     */
    virtual void registerTouchscreenSwipeShortcut(SwipeDirection direction,
                                                  uint fingerCount,
                                                  QAction* action,
                                                  std::function<void(qreal)> progressCallback)
        = 0;

    /**
     * Retrieve the proxy class for an effect if it has one. Will return NULL if
     * the effect isn't loaded or doesn't have a proxy class.
     */
    virtual void* getProxy(QString name) = 0;

    // Mouse polling
    virtual void startMousePolling() = 0;
    virtual void stopMousePolling() = 0;

    virtual void reserveElectricBorder(ElectricBorder border, Effect* effect) = 0;
    virtual void unreserveElectricBorder(ElectricBorder border, Effect* effect) = 0;

    /**
     * Registers the given @p action for the given @p border to be activated through
     * a touch swipe gesture.
     *
     * If the @p border gets triggered through a touch swipe gesture the QAction::triggered
     * signal gets invoked.
     *
     * To unregister the touch screen action either delete the @p action or
     * invoke unregisterTouchBorder.
     *
     * @see unregisterTouchBorder
     * @since 5.10
     */
    virtual void registerTouchBorder(ElectricBorder border, QAction* action) = 0;

    /**
     * Registers the given @p action for the given @p border to be activated through
     * a touch swipe gesture.
     *
     * If the @p border gets triggered through a touch swipe gesture the QAction::triggered
     * signal gets invoked.
     *
     * progressCallback will be dinamically called each time the touch position is updated
     * to show the effect "partially" activated
     *
     * To unregister the touch screen action either delete the @p action or
     * invoke unregisterTouchBorder.
     *
     * @see unregisterTouchBorder
     * @since 5.25
     */
    virtual void registerRealtimeTouchBorder(ElectricBorder border,
                                             QAction* action,
                                             TouchBorderCallback progressCallback)
        = 0;

    /**
     * Unregisters the given @p action for the given touch @p border.
     *
     * @see registerTouchBorder
     * @since 5.10
     */
    virtual void unregisterTouchBorder(ElectricBorder border, QAction* action) = 0;

    // functions that allow controlling windows/desktop
    virtual void activateWindow(KWin::EffectWindow* c) = 0;
    virtual KWin::EffectWindow* activeWindow() const = 0;
    Q_SCRIPTABLE virtual void
    moveWindow(KWin::EffectWindow* w, const QPoint& pos, bool snap = false, double snapAdjust = 1.0)
        = 0;

    /**
     * Moves the window to the specific desktop
     * Setting desktop to NET::OnAllDesktops will set the window on all desktops
     */
    Q_SCRIPTABLE virtual void windowToDesktop(KWin::EffectWindow* w, int desktop) = 0;

    /**
     * Moves a window to the given desktops
     * On X11, the window will end up on the last window in the list
     * Setting this to an empty list will set the window on all desktops
     *
     * @arg desktopIds a list of desktops the window should be placed on. NET::OnAllDesktops is not
     * a valid desktop X11Id
     */
    Q_SCRIPTABLE virtual void windowToDesktops(KWin::EffectWindow* w,
                                               const QVector<uint>& desktopIds)
        = 0;

    Q_SCRIPTABLE virtual void windowToScreen(KWin::EffectWindow* w, EffectScreen* screen) = 0;
    virtual void setShowingDesktop(bool showing) = 0;

    // Activities
    /**
     * @returns The ID of the current activity.
     */
    virtual QString currentActivity() const = 0;
    // Desktops
    /**
     * @returns The ID of the current desktop.
     */
    virtual int currentDesktop() const = 0;
    /**
     * @returns Total number of desktops currently in existence.
     */
    virtual int numberOfDesktops() const = 0;
    /**
     * Set the current desktop to @a desktop.
     */
    virtual void setCurrentDesktop(int desktop) = 0;
    /**
     * Sets the total number of desktops to @a desktops.
     */
    virtual void setNumberOfDesktops(int desktops) = 0;
    /**
     * @returns The size of desktop layout in grid units.
     */
    virtual QSize desktopGridSize() const = 0;
    /**
     * @returns The width of desktop layout in grid units.
     */
    virtual int desktopGridWidth() const = 0;
    /**
     * @returns The height of desktop layout in grid units.
     */
    virtual int desktopGridHeight() const = 0;
    /**
     * @returns The width of desktop layout in pixels.
     */
    virtual int workspaceWidth() const = 0;
    /**
     * @returns The height of desktop layout in pixels.
     */
    virtual int workspaceHeight() const = 0;
    /**
     * @returns The ID of the desktop at the point @a coords or 0 if no desktop exists at that
     * point. @a coords is to be in grid units.
     */
    virtual int desktopAtCoords(QPoint coords) const = 0;
    /**
     * @returns The coords of desktop @a id in grid units.
     */
    virtual QPoint desktopGridCoords(int id) const = 0;
    /**
     * @returns The coords of the top-left corner of desktop @a id in pixels.
     */
    virtual QPoint desktopCoords(int id) const = 0;
    /**
     * @returns The ID of the desktop above desktop @a id. Wraps around to the bottom of
     * the layout if @a wrap is set. If @a id is not set use the current one.
     */
    Q_SCRIPTABLE virtual int desktopAbove(int desktop = 0, bool wrap = true) const = 0;
    /**
     * @returns The ID of the desktop to the right of desktop @a id. Wraps around to the
     * left of the layout if @a wrap is set. If @a id is not set use the current one.
     */
    Q_SCRIPTABLE virtual int desktopToRight(int desktop = 0, bool wrap = true) const = 0;
    /**
     * @returns The ID of the desktop below desktop @a id. Wraps around to the top of the
     * layout if @a wrap is set. If @a id is not set use the current one.
     */
    Q_SCRIPTABLE virtual int desktopBelow(int desktop = 0, bool wrap = true) const = 0;
    /**
     * @returns The ID of the desktop to the left of desktop @a id. Wraps around to the
     * right of the layout if @a wrap is set. If @a id is not set use the current one.
     */
    Q_SCRIPTABLE virtual int desktopToLeft(int desktop = 0, bool wrap = true) const = 0;
    Q_SCRIPTABLE virtual QString desktopName(int desktop) const = 0;
    virtual bool optionRollOverDesktops() const = 0;

    virtual EffectScreen* activeScreen() const = 0; // Xinerama
    virtual QRect clientArea(clientAreaOption, EffectScreen const* screen, int desktop) const = 0;
    virtual QRect clientArea(clientAreaOption, const EffectWindow* c) const = 0;
    virtual QRect clientArea(clientAreaOption, const QPoint& p, int desktop) const = 0;

    /**
     * The bounding size of all screens combined. Overlapping areas
     * are not counted multiple times.
     *
     * @see virtualScreenGeometry()
     * @see virtualScreenSizeChanged()
     * @since 5.0
     */
    virtual QSize virtualScreenSize() const = 0;
    /**
     * The bounding geometry of all outputs combined. Always starts at (0,0) and has
     * virtualScreenSize as it's size.
     *
     * @see virtualScreenSize()
     * @see virtualScreenGeometryChanged()
     * @since 5.0
     */
    virtual QRect virtualScreenGeometry() const = 0;
    /**
     * Factor by which animation speed in the effect should be modified (multiplied).
     * If configurable in the effect itself, the option should have also 'default'
     * animation speed. The actual value should be determined using animationTime().
     * Note: The factor can be also 0, so make sure your code can cope with 0ms time
     * if used manually.
     */
    virtual double animationTimeFactor() const = 0;
    virtual WindowQuadType newWindowQuadType() = 0;

    Q_SCRIPTABLE KWin::EffectWindow* findWindow(WId id) const;
    Q_SCRIPTABLE KWin::EffectWindow* findWindow(Wrapland::Server::Surface* surf) const;
    /**
     * Finds the EffectWindow for the internal window @p w.
     * If there is no such window @c null is returned.
     *
     * On Wayland this returns the internal window. On X11 it returns an Unamanged with the
     * window id matching that of the provided window @p w.
     *
     * @since 5.16
     */
    Q_SCRIPTABLE KWin::EffectWindow* findWindow(QWindow* w) const;
    /**
     * Finds the EffectWindow for the Toplevel with KWin internal @p id.
     * If there is no such window @c null is returned.
     *
     * @since 5.16
     */
    Q_SCRIPTABLE KWin::EffectWindow* findWindow(const QUuid& id) const;

    virtual EffectWindowList stackingOrder() const = 0;
    // window will be temporarily painted as if being at the top of the stack
    Q_SCRIPTABLE virtual void setElevatedWindow(KWin::EffectWindow* w, bool set) = 0;

    virtual void setTabBoxWindow(EffectWindow*) = 0;
    virtual void setTabBoxDesktop(int) = 0;
    virtual EffectWindowList currentTabBoxWindowList() const = 0;
    virtual void refTabBox() = 0;
    virtual void unrefTabBox() = 0;
    virtual void closeTabBox() = 0;
    virtual QList<int> currentTabBoxDesktopList() const = 0;
    virtual int currentTabBoxDesktop() const = 0;
    virtual EffectWindow* currentTabBoxWindow() const = 0;

    virtual void setActiveFullScreenEffect(Effect* e) = 0;
    virtual Effect* activeFullScreenEffect() const = 0;

    /**
     * Schedules the entire workspace to be repainted next time.
     * If you call it during painting (including prepaint) then it does not
     *  affect the current painting.
     */
    Q_SCRIPTABLE virtual void addRepaintFull() = 0;
    Q_SCRIPTABLE virtual void addRepaint(const QRect& r) = 0;
    Q_SCRIPTABLE virtual void addRepaint(const QRegion& r) = 0;
    Q_SCRIPTABLE virtual void addRepaint(int x, int y, int w, int h) = 0;

    Q_SCRIPTABLE virtual bool isEffectLoaded(const QString& name) const = 0;

    CompositingType compositingType() const;
    /**
     * @brief Whether the Compositor is OpenGL based (either GL 1 or 2).
     *
     * @return bool @c true in case of OpenGL based Compositor, @c false otherwise
     */
    bool isOpenGLCompositing() const;
    /**
     * @brief Provides access to the QPainter which is rendering to the back buffer.
     *
     * Only relevant for CompositingType QPainterCompositing. For all other compositing types
     * @c null is returned.
     *
     * @return QPainter* The Scene's QPainter or @c null.
     */
    virtual QPainter* scenePainter() = 0;
    virtual void reconfigure() = 0;

    virtual QByteArray readRootProperty(long atom, long type, int format) const = 0;
    /**
     * @brief Announces support for the feature with the given name. If no other Effect
     * has announced support for this feature yet, an X11 property will be installed on
     * the root window.
     *
     * The Effect will be notified for events through the signal propertyNotify().
     *
     * To remove the support again use removeSupportProperty. When an Effect is
     * destroyed it is automatically taken care of removing the support. It is not
     * required to call removeSupportProperty in the Effect's cleanup handling.
     *
     * @param propertyName The name of the property to announce support for
     * @param effect The effect which announces support
     * @return xcb_atom_t The created X11 atom
     * @see removeSupportProperty
     * @since 4.11
     */
    virtual xcb_atom_t announceSupportProperty(const QByteArray& propertyName, Effect* effect) = 0;
    /**
     * @brief Removes support for the feature with the given name. If there is no other Effect left
     * which has announced support for the given property, the property will be removed from the
     * root window.
     *
     * In case the Effect had not registered support, calling this function does not change
     * anything.
     *
     * @param propertyName The name of the property to remove support for
     * @param effect The effect which had registered the property.
     * @see announceSupportProperty
     * @since 4.11
     */
    virtual void removeSupportProperty(const QByteArray& propertyName, Effect* effect) = 0;

    /**
     * Returns @a true if the active window decoration has shadow API hooks.
     */
    virtual bool hasDecorationShadows() const = 0;

    /**
     * Returns @a true if the window decorations use the alpha channel, and @a false otherwise.
     * @since 4.5
     */
    virtual bool decorationsHaveAlpha() const = 0;

    /**
     * Creates a new frame object. If the frame does not have a static size
     * then it will be located at @a position with @a alignment. A
     * non-static frame will automatically adjust its size to fit the contents.
     * @returns A new @ref EffectFrame. It is the responsibility of the caller to delete the
     * EffectFrame.
     * @since 4.6
     */
    virtual std::unique_ptr<EffectFrame> effectFrame(EffectFrameStyle style,
                                                     bool staticSize = true,
                                                     const QPoint& position = QPoint(-1, -1),
                                                     Qt::Alignment alignment
                                                     = Qt::AlignCenter) const
        = 0;

    /**
     * Allows an effect to trigger a reload of itself.
     * This can be used by an effect which needs to be reloaded when screen geometry changes.
     * It is possible that the effect cannot be loaded again as it's supported method does no longer
     * hold.
     * @param effect The effect to reload
     * @since 4.8
     */
    virtual void reloadEffect(Effect* effect) = 0;

    /**
     * Whether the screen is currently considered as locked.
     * Note for technical reasons this is not always possible to detect. The screen will only
     * be considered as locked if the screen locking process implements the
     * org.freedesktop.ScreenSaver interface.
     *
     * @returns @c true if the screen is currently locked, @c false otherwise
     * @see screenLockingChanged
     * @since 4.11
     */
    virtual bool isScreenLocked() const = 0;

    /**
     * @brief Makes the OpenGL compositing context current.
     *
     * If the compositing backend is not using OpenGL, this method returns @c false.
     *
     * @return bool @c true if the context became current, @c false otherwise.
     */
    virtual bool makeOpenGLContextCurrent() = 0;
    /**
     * @brief Makes a null OpenGL context current resulting in no context
     * being current.
     *
     * If the compositing backend is not OpenGL based, this method is a noop.
     *
     * There is normally no reason for an Effect to call this method.
     */
    virtual void doneOpenGLContextCurrent() = 0;

    virtual xcb_connection_t* xcbConnection() const = 0;
    virtual xcb_window_t x11RootWindow() const = 0;

    /**
     * Interface to the Wayland display: this is relevant only
     * on Wayland, on X11 it will be nullptr
     * @since 5.5
     */
    virtual Wrapland::Server::Display* waylandDisplay() const = 0;

    /**
     * Whether animations are supported by the Scene.
     * If this method returns @c false Effects are supposed to not
     * animate transitions.
     *
     * @returns Whether the Scene can drive animations
     * @since 5.8
     */
    virtual bool animationsSupported() const = 0;

    /**
     * The current cursor image of the Platform.
     * @see cursorPos
     * @since 5.9
     */
    virtual PlatformCursorImage cursorImage() const = 0;

    /**
     * The cursor image should be hidden.
     * @see showCursor
     * @since 5.9
     */
    virtual void hideCursor() = 0;

    /**
     * The cursor image should be shown again after having been hidden.
     * @see hideCursor
     * @since 5.9
     */
    virtual void showCursor() = 0;

    /**
     * @returns Whether or not the cursor is currently hidden
     */
    virtual bool isCursorHidden() const = 0;

    /**
     * Starts an interactive window selection process.
     *
     * Once the user selected a window the @p callback is invoked with the selected EffectWindow as
     * argument. In case the user cancels the interactive window selection or selecting a window is
     * currently not possible (e.g. screen locked) the @p callback is invoked with a @c nullptr
     * argument.
     *
     * During the interactive window selection the cursor is turned into a crosshair cursor.
     *
     * @param callback The function to invoke once the interactive window selection ends
     * @since 5.9
     */
    virtual void startInteractiveWindowSelection(std::function<void(KWin::EffectWindow*)> callback)
        = 0;

    /**
     * Starts an interactive position selection process.
     *
     * Once the user selected a position on the screen the @p callback is invoked with
     * the selected point as argument. In case the user cancels the interactive position selection
     * or selecting a position is currently not possible (e.g. screen locked) the @p callback
     * is invoked with a point at @c -1 as x and y argument.
     *
     * During the interactive window selection the cursor is turned into a crosshair cursor.
     *
     * @param callback The function to invoke once the interactive position selection ends
     * @since 5.9
     */
    virtual void startInteractivePositionSelection(std::function<void(const QPoint&)> callback) = 0;

    /**
     * Shows an on-screen-message. To hide it again use hideOnScreenMessage.
     *
     * @param message The message to show
     * @param iconName The optional themed icon name
     * @see hideOnScreenMessage
     * @since 5.9
     */
    virtual void showOnScreenMessage(const QString& message, const QString& iconName = QString())
        = 0;

    /**
     * Flags for how to hide a shown on-screen-message
     * @see hideOnScreenMessage
     * @since 5.9
     */
    enum class OnScreenMessageHideFlag {
        /**
         * The on-screen-message should skip the close window animation.
         * @see EffectWindow::skipsCloseAnimation
         */
        SkipsCloseAnimation = 1
    };
    Q_DECLARE_FLAGS(OnScreenMessageHideFlags, OnScreenMessageHideFlag)
    /**
     * Hides a previously shown on-screen-message again.
     * @param flags The flags for how to hide the message
     * @see showOnScreenMessage
     * @since 5.9
     */
    virtual void hideOnScreenMessage(OnScreenMessageHideFlags flags = OnScreenMessageHideFlags())
        = 0;

    /*
     * @returns The configuration used by the EffectsHandler.
     * @since 5.10
     */
    virtual KSharedConfigPtr config() const = 0;

    /**
     * @returns The global input configuration (kcminputrc)
     * @since 5.10
     */
    virtual KSharedConfigPtr inputConfig() const = 0;

    /**
     * Returns if activeFullScreenEffect is set
     */
    virtual bool hasActiveFullScreenEffect() const = 0;

    /**
     * Render the supplied EffectQuickView onto the scene
     * It can be called at any point during the scene rendering
     * @since 5.18
     */
    virtual void renderEffectQuickView(EffectQuickView* effectQuickView) const = 0;

    /**
     * The status of the session i.e if the user is logging out
     * @since 5.18
     */
    virtual SessionState sessionState() const = 0;

    /**
     * Returns the list of all the screens connected to the system.
     */
    virtual QList<EffectScreen*> screens() const = 0;
    virtual EffectScreen* screenAt(const QPoint& point) const = 0;
    virtual EffectScreen* findScreen(const QString& name) const = 0;
    virtual EffectScreen* findScreen(int screenId) const = 0;

    virtual effect::region_integration& get_blur_integration() = 0;
    virtual effect::color_integration& get_contrast_integration() = 0;
    virtual effect::anim_integration& get_slide_integration() = 0;
    virtual effect::kscreen_integration& get_kscreen_integration() = 0;

    virtual QImage blit_from_framebuffer(QRect const& geometry, double scale) const = 0;

    /**
     * Returns the rect that's currently being repainted, in the logical pixels.
     */
    virtual QRect renderTargetRect() const = 0;
    /**
     * Returns the device pixel ratio of the current render target.
     */
    virtual qreal renderTargetScale() const = 0;

    /**
     * Maps the given @a rect from the global screen cordinates to the render
     * target local coordinate system.
     */
    QRect mapToRenderTarget(QRect const& rect) const;
    /**
     * Maps the given @a region from the global screen coordinates to the render
     * target local coordinate system.
     */
    QRegion mapToRenderTarget(QRegion const& region) const;

Q_SIGNALS:
    /**
     * This signal is emitted whenever a new @a screen is added to the system.
     */
    void screenAdded(KWin::EffectScreen* screen);
    /**
     * This signal is emitted whenever a @a screen is removed from the system.
     */
    void screenRemoved(KWin::EffectScreen* screen);
    /**
     * Signal emitted when the current desktop changed.
     * @param oldDesktop The previously current desktop
     * @param newDesktop The new current desktop
     * @param with The window which is taken over to the new desktop, can be NULL
     * @since 4.9
     */
    void desktopChanged(int oldDesktop, int newDesktop, KWin::EffectWindow* with);

    /**
     * Signal emmitted while desktop is changing for animation.
     * @param currentDesktop The current desktop untiotherwise.
     * @param offset The current desktop offset.
     * offset.x() = .6 means 60% of the way to the desktop to the right.
     * Positive Values means Up and Right.
     */
    void desktopChanging(uint currentDesktop, QPointF offset, KWin::EffectWindow* with);
    void desktopChangingCancelled();

    /**
     * @since 4.7
     * @deprecated
     */
    void KWIN_DEPRECATED desktopChanged(int oldDesktop, int newDesktop);
    /**
     * @internal
     */
    void desktopChangedLegacy(int oldDesktop, int newDesktop);
    /**
     * Signal emitted when a window moved to another desktop
     * NOTICE that this does NOT imply that the desktop has changed
     * The @param window which is moved to the new desktop
     * @param oldDesktop The previous desktop of the window
     * @param newDesktop The new desktop of the window
     * @since 4.11.4
     */
    void desktopPresenceChanged(KWin::EffectWindow* window, int oldDesktop, int newDesktop);
    /**
     * Emitted when the virtual desktop grid layout changes
     * @param size new size
     * @since 5.25
     */
    void desktopGridSizeChanged(const QSize& size);
    /**
     * Emitted when the virtual desktop grid layout changes
     * @param width new width
     * @since 5.25
     */
    void desktopGridWidthChanged(int width);
    /**
     * Emitted when the virtual desktop grid layout changes
     * @param height new height
     * @since 5.25
     */
    void desktopGridHeightChanged(int height);
    /**
     * Signal emitted when the number of currently existing desktops is changed.
     * @param old The previous number of desktops in used.
     * @see EffectsHandler::numberOfDesktops.
     * @since 4.7
     */
    void numberDesktopsChanged(uint old);
    /**
     * Signal emitted when the desktop showing ("dashboard") state changed
     * The desktop is risen to the keepAbove layer, you may want to elevate
     * windows or such.
     * @since 5.3
     */
    void showingDesktopChanged(bool);
    /**
     * Signal emitted when a new window has been added to the Workspace.
     * @param w The added window
     * @since 4.7
     */
    void windowAdded(KWin::EffectWindow* w);
    /**
     * Signal emitted when a window is being removed from the Workspace.
     * An effect which wants to animate the window closing should connect
     * to this signal and reference the window by using
     * refWindow
     * @param w The window which is being closed
     * @since 4.7
     */
    void windowClosed(KWin::EffectWindow* w);
    /**
     * Signal emitted when a window get's activated.
     * @param w The new active window, or @c NULL if there is no active window.
     * @since 4.7
     */
    void windowActivated(KWin::EffectWindow* w);
    /**
     * Signal emitted when a window is deleted.
     * This means that a closed window is not referenced any more.
     * An effect bookkeeping the closed windows should connect to this
     * signal to clean up the internal references.
     * @param w The window which is going to be deleted.
     * @see EffectWindow::refWindow
     * @see EffectWindow::unrefWindow
     * @see windowClosed
     * @since 4.7
     */
    void windowDeleted(KWin::EffectWindow* w);
    /**
     * Signal emitted when a user begins a window move or resize operation.
     * To figure out whether the user resizes or moves the window use
     * isUserMove or isUserResize.
     * Whenever the geometry is updated the signal @ref windowStepUserMovedResized
     * is emitted with the current geometry.
     * The move/resize operation ends with the signal @ref windowFinishUserMovedResized.
     * Only one window can be moved/resized by the user at the same time!
     * @param w The window which is being moved/resized
     * @see windowStepUserMovedResized
     * @see windowFinishUserMovedResized
     * @see EffectWindow::isUserMove
     * @see EffectWindow::isUserResize
     * @since 4.7
     */
    void windowStartUserMovedResized(KWin::EffectWindow* w);
    /**
     * Signal emitted during a move/resize operation when the user changed the geometry.
     * Please note: KWin supports two operation modes. In one mode all changes are applied
     * instantly. This means the window's geometry matches the passed in @p geometry. In the
     * other mode the geometry is changed after the user ended the move/resize mode.
     * The @p geometry differs from the window's geometry. Also the window's pixmap still has
     * the same size as before. Depending what the effect wants to do it would be recommended
     * to scale/translate the window.
     * @param w The window which is being moved/resized
     * @param geometry The geometry of the window in the current move/resize step.
     * @see windowStartUserMovedResized
     * @see windowFinishUserMovedResized
     * @see EffectWindow::isUserMove
     * @see EffectWindow::isUserResize
     * @since 4.7
     */
    void windowStepUserMovedResized(KWin::EffectWindow* w, const QRect& geometry);
    /**
     * Signal emitted when the user finishes move/resize of window @p w.
     * @param w The window which has been moved/resized
     * @see windowStartUserMovedResized
     * @see windowFinishUserMovedResized
     * @since 4.7
     */
    void windowFinishUserMovedResized(KWin::EffectWindow* w);
    /**
     * Signal emitted when the maximized state of the window @p w changed.
     * A window can be in one of four states:
     * @li restored: both @p horizontal and @p vertical are @c false
     * @li horizontally maximized: @p horizontal is @c true and @p vertical is @c false
     * @li vertically maximized: @p horizontal is @c false and @p vertical is @c true
     * @li completely maximized: both @p horizontal and @p vertical are @c true
     * @param w The window whose maximized state changed
     * @param horizontal If @c true maximized horizontally
     * @param vertical If @c true maximized vertically
     * @since 4.7
     */
    void windowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical);
    /**
     * Signal emitted when the geometry or shape of a window changed.
     * This is caused if the window changes geometry without user interaction.
     * E.g. the decoration is changed. This is in opposite to windowUserMovedResized
     * which is caused by direct user interaction.
     * @param w The window whose geometry changed
     * @param old The previous geometry
     * @see windowUserMovedResized
     * @since 4.7
     */
    void windowGeometryShapeChanged(KWin::EffectWindow* w, const QRect& old);
    /**
     * This signal is emitted when the frame geometry of a window changed.
     * @param window The window whose geometry changed
     * @param oldGeometry The previous geometry
     * @since 5.19
     */
    void windowFrameGeometryChanged(KWin::EffectWindow* window, const QRect& oldGeometry);
    /**
     * Signal emitted when the padding of a window changed. (eg. shadow size)
     * @param w The window whose geometry changed
     * @param old The previous expandedGeometry()
     * @since 4.9
     */
    void windowPaddingChanged(KWin::EffectWindow* w, const QRect& old);
    /**
     * Signal emitted when the windows opacity is changed.
     * @param w The window whose opacity level is changed.
     * @param oldOpacity The previous opacity level
     * @param newOpacity The new opacity level
     * @since 4.7
     */
    void windowOpacityChanged(KWin::EffectWindow* w, qreal oldOpacity, qreal newOpacity);
    /**
     * Signal emitted when a window got minimized.
     * @param w The window which was minimized
     * @since 4.7
     */
    void windowMinimized(KWin::EffectWindow* w);
    /**
     * Signal emitted when a window got unminimized.
     * @param w The window which was unminimized
     * @since 4.7
     */
    void windowUnminimized(KWin::EffectWindow* w);
    /**
     * Signal emitted when a window either becomes modal (ie. blocking for its main client) or
     * looses that state.
     * @param w The window which was unminimized
     * @since 4.11
     */
    void windowModalityChanged(KWin::EffectWindow* w);
    /**
     * Signal emitted when a window either became unresponsive (eg. app froze or crashed)
     * or respoonsive
     * @param w The window that became (un)responsive
     * @param unresponsive Whether the window is responsive or unresponsive
     * @since 5.10
     */
    void windowUnresponsiveChanged(KWin::EffectWindow* w, bool unresponsive);
    /**
     * Signal emitted when an area of a window is scheduled for repainting.
     * Use this signal in an effect if another area needs to be synced as well.
     * @param w The window which is scheduled for repainting
     * @param r Always empty.
     * @since 4.7
     */
    void windowDamaged(KWin::EffectWindow* w, const QRegion& r);
    /**
     * Signal emitted when a tabbox is added.
     * An effect who wants to replace the tabbox with itself should use refTabBox.
     * @param mode The TabBoxMode.
     * @see refTabBox
     * @see tabBoxClosed
     * @see tabBoxUpdated
     * @see tabBoxKeyEvent
     * @since 4.7
     */
    void tabBoxAdded(int mode);
    /**
     * Signal emitted when the TabBox was closed by KWin core.
     * An effect which referenced the TabBox should use unrefTabBox to unref again.
     * @see unrefTabBox
     * @see tabBoxAdded
     * @since 4.7
     */
    void tabBoxClosed();
    /**
     * Signal emitted when the selected TabBox window changed or the TabBox List changed.
     * An effect should only response to this signal if it referenced the TabBox with refTabBox.
     * @see refTabBox
     * @see currentTabBoxWindowList
     * @see currentTabBoxDesktopList
     * @see currentTabBoxWindow
     * @see currentTabBoxDesktop
     * @since 4.7
     */
    void tabBoxUpdated();
    /**
     * Signal emitted when a key event, which is not handled by TabBox directly is, happens while
     * TabBox is active. An effect might use the key event to e.g. change the selected window.
     * An effect should only response to this signal if it referenced the TabBox with refTabBox.
     * @param event The key event not handled by TabBox directly
     * @see refTabBox
     * @since 4.7
     */
    void tabBoxKeyEvent(QKeyEvent* event);
    void currentTabAboutToChange(KWin::EffectWindow* from, KWin::EffectWindow* to);
    void tabAdded(KWin::EffectWindow* from, KWin::EffectWindow* to);   // from merged with to
    void tabRemoved(KWin::EffectWindow* c, KWin::EffectWindow* group); // c removed from group
    /**
     * Signal emitted when mouse changed.
     * If an effect needs to get updated mouse positions, it needs to first call startMousePolling.
     * For a fullscreen effect it is better to use an input window and react on
     * windowInputMouseEvent.
     * @param pos The new mouse position
     * @param oldpos The previously mouse position
     * @param buttons The pressed mouse buttons
     * @param oldbuttons The previously pressed mouse buttons
     * @param modifiers Pressed keyboard modifiers
     * @param oldmodifiers Previously pressed keyboard modifiers.
     * @see startMousePolling
     * @since 4.7
     */
    void mouseChanged(const QPoint& pos,
                      const QPoint& oldpos,
                      Qt::MouseButtons buttons,
                      Qt::MouseButtons oldbuttons,
                      Qt::KeyboardModifiers modifiers,
                      Qt::KeyboardModifiers oldmodifiers);
    /**
     * Signal emitted when the cursor shape changed.
     * You'll likely want to query the current cursor as reaction:
     * xcb_xfixes_get_cursor_image_unchecked Connection to this signal is tracked, so if you don't
     * need it anymore, disconnect from it to stop cursor event filtering
     */
    void cursorShapeChanged();
    /**
     * Receives events registered for using registerPropertyType.
     * Use readProperty() to get the property data.
     * Note that the property may be already set on the window, so doing the same
     * processing from windowAdded() (e.g. simply calling propertyNotify() from it)
     * is usually needed.
     * @param w The window whose property changed, is @c null if it is a root window property
     * @param atom The property
     * @since 4.7
     */
    void propertyNotify(KWin::EffectWindow* w, long atom);

    /**
     * Signal emitted after the screen geometry changed (e.g. add of a monitor).
     * Effects using displayWidth()/displayHeight() to cache information should
     * react on this signal and update the caches.
     * @param size The new screen size
     * @since 4.8
     */
    void screenGeometryChanged(const QSize& size);

    /**
     * This signal is emitted when the global
     * activity is changed
     * @param id id of the new current activity
     * @since 4.9
     */
    void currentActivityChanged(const QString& id);
    /**
     * This signal is emitted when a new activity is added
     * @param id id of the new activity
     * @since 4.9
     */
    void activityAdded(const QString& id);
    /**
     * This signal is emitted when the activity
     * is removed
     * @param id id of the removed activity
     * @since 4.9
     */
    void activityRemoved(const QString& id);
    /**
     * This signal is emitted when the screen got locked or unlocked.
     * @param locked @c true if the screen is now locked, @c false if it is now unlocked
     * @since 4.11
     */
    void screenLockingChanged(bool locked);

    /**
     * This signal is emitted just before the screen locker tries to grab keys and lock the screen
     * Effects should release any grabs immediately
     * @since 5.17
     */
    void screenAboutToLock();

    /**
     * This signels is emitted when ever the stacking order is change, ie. a window is risen
     * or lowered
     * @since 4.10
     */
    void stackingOrderChanged();
    /**
     * This signal is emitted when the user starts to approach the @p border with the mouse.
     * The @p factor describes how far away the mouse is in a relative mean. The values are in
     * [0.0, 1.0] with 0.0 being emitted when first entered and on leaving. The value 1.0 means that
     * the @p border is reached with the mouse. So the values are well suited for animations.
     * The signal is always emitted when the mouse cursor position changes.
     * @param border The screen edge which is being approached
     * @param factor Value in range [0.0,1.0] to describe how close the mouse is to the border
     * @param geometry The geometry of the edge which is being approached
     * @since 4.11
     */
    void screenEdgeApproaching(ElectricBorder border, qreal factor, const QRect& geometry);
    /**
     * Emitted whenever the virtualScreenSize changes.
     * @see virtualScreenSize()
     * @since 5.0
     */
    void virtualScreenSizeChanged();
    /**
     * Emitted whenever the virtualScreenGeometry changes.
     * @see virtualScreenGeometry()
     * @since 5.0
     */
    void virtualScreenGeometryChanged();

    /**
     * The window @p w gets shown again. The window was previously
     * initially shown with windowAdded and hidden with windowHidden.
     *
     * @see windowHidden
     * @see windowAdded
     * @since 5.8
     */
    void windowShown(KWin::EffectWindow* w);

    /**
     * The window @p w got hidden but not yet closed.
     * This can happen when a window is still being used and is supposed to be shown again
     * with windowShown. On X11 an example is autohiding panels. On Wayland every
     * window first goes through the window hidden state and might get shown again, or might
     * get closed the normal way.
     *
     * @see windowShown
     * @see windowClosed
     * @since 5.8
     */
    void windowHidden(KWin::EffectWindow* w);

    /**
     * This signal gets emitted when the data on EffectWindow @p w for @p role changed.
     *
     * An Effect can connect to this signal to read the new value and react on it.
     * E.g. an Effect which does not operate on windows grabbed by another Effect wants
     * to cancel the already scheduled animation if another Effect adds a grab.
     *
     * @param w The EffectWindow for which the data changed
     * @param role The data role which changed
     * @see EffectWindow::setData
     * @see EffectWindow::data
     * @since 5.8.4
     */
    void windowDataChanged(KWin::EffectWindow* w, int role);

    /**
     * The xcb connection changed, either a new xcbConnection got created or the existing one
     * got destroyed.
     * Effects can use this to refetch the properties they want to set.
     *
     * When the xcbConnection changes also the x11RootWindow becomes invalid.
     * @see xcbConnection
     * @see x11RootWindow
     * @since 5.11
     */
    void xcbConnectionChanged();

    /**
     * This signal is emitted when active fullscreen effect changed.
     *
     * @see activeFullScreenEffect
     * @see setActiveFullScreenEffect
     * @since 5.14
     */
    void activeFullScreenEffectChanged();

    /**
     * This signal is emitted when active fullscreen effect changed to being
     * set or unset
     *
     * @see activeFullScreenEffect
     * @see setActiveFullScreenEffect
     * @since 5.15
     */
    void hasActiveFullScreenEffectChanged();

    /**
     * This signal is emitted when the keep above state of @p w was changed.
     *
     * @param w The window whose the keep above state was changed.
     * @since 5.15
     */
    void windowKeepAboveChanged(KWin::EffectWindow* w);

    /**
     * This signal is emitted when the keep below state of @p was changed.
     *
     * @param w The window whose the keep below state was changed.
     * @since 5.15
     */
    void windowKeepBelowChanged(KWin::EffectWindow* w);

    /**
     * This signal is emitted when the full screen state of @p w was changed.
     *
     * @param w The window whose the full screen state was changed.
     * @since 5.15
     */
    void windowFullScreenChanged(KWin::EffectWindow* w);

    /**
     * This signal is emitted when the session state was changed
     * @since 5.18
     */
    void sessionStateChanged();

    void startupAdded(const QString& id, const QIcon& icon);
    void startupChanged(const QString& id, const QIcon& icon);
    void startupRemoved(const QString& id);

    /**
     * This signal is emitted when the visible geometry of a window changed.
     */
    void windowExpandedGeometryChanged(KWin::EffectWindow* window);

    void frameRendered();

    void globalShortcutChanged(QAction* action, QKeySequence const& seq);

protected:
    virtual EffectWindow* find_window_by_wid(WId id) const = 0;
    virtual EffectWindow* find_window_by_surface(Wrapland::Server::Surface* surface) const = 0;
    virtual EffectWindow* find_window_by_qwindow(QWindow* window) const = 0;
    virtual EffectWindow* find_window_by_uuid(QUuid const& id) const = 0;

    QVector<EffectPair> loaded_effects;
    // QHash< QString, EffectFactory* > effect_factories;
    CompositingType compositing_type;
};

/**
 * Pointer to the global EffectsHandler object.
 */
extern KWINEFFECTS_EXPORT EffectsHandler* effects;

}
