/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/export.h>
#include <kwineffects/types.h>
#include <kwinglobals.h>

#include <KSharedConfig>
#include <QKeyEvent>

namespace KWin
{

class EffectFrame;
class EffectWindow;
class ScreenPrePaintData;
class ScreenPaintData;
class WindowQuadList;
class WindowPrePaintData;
class WindowPaintData;

/**
 * @short Base class for all KWin effects
 *
 * This is the base class for all effects. By reimplementing virtual methods
 *  of this class, you can customize how the windows are painted.
 *
 * The virtual methods are used for painting and need to be implemented for
 * custom painting.
 *
 * In order to react to state changes (e.g. a window gets closed) the effect
 * should provide slots for the signals emitted by the EffectsHandler.
 *
 * @section Chaining
 * Most methods of this class are called in chain style. This means that when
 *  effects A and B area active then first e.g. A::paintWindow() is called and
 *  then from within that method B::paintWindow() is called (although
 *  indirectly). To achieve this, you need to make sure to call corresponding
 *  method in EffectsHandler class from each such method (using @ref effects
 *  pointer):
 * @code
 *  void MyEffect::postPaintScreen()
 *  {
 *      // Do your own processing here
 *      ...
 *      // Call corresponding EffectsHandler method
 *      effects->postPaintScreen();
 *  }
 * @endcode
 *
 * @section Effectsptr Effects pointer
 * @ref effects pointer points to the global EffectsHandler object that you can
 *  use to interact with the windows.
 *
 * @section painting Painting stages
 * Painting of windows is done in three stages:
 * @li First, the prepaint pass.<br>
 *  Here you can specify how the windows will be painted, e.g. that they will
 *  be translucent and transformed.
 * @li Second, the paint pass.<br>
 *  Here the actual painting takes place. You can change attributes such as
 *  opacity of windows as well as apply transformations to them. You can also
 *  paint something onto the screen yourself.
 * @li Finally, the postpaint pass.<br>
 *  Here you can mark windows, part of windows or even the entire screen for
 *  repainting to create animations.
 *
 * For each stage there are *Screen() and *Window() methods. The window method
 *  is called for every window while the screen method is usually called just
 *  once.
 *
 * @section OpenGL
 * Effects can use OpenGL if EffectsHandler::isOpenGLCompositing() returns @c true.
 * The OpenGL context may not always be current when code inside the effect is
 * executed. The framework ensures that the OpenGL context is current when the Effect
 * gets created, destroyed or reconfigured and during the painting stages. All virtual
 * methods which have the OpenGL context current are documented.
 *
 * If OpenGL code is going to be executed outside the painting stages, e.g. in reaction
 * to a global shortcut, it is the task of the Effect to make the OpenGL context current:
 * @code
 * effects->makeOpenGLContextCurrent();
 * @endcode
 *
 * There is in general no need to call the matching doneCurrent method.
 */
class KWINEFFECTS_EXPORT Effect : public QObject
{
    Q_OBJECT
public:
    /** Flags controlling how painting is done. */
    // TODO: is that ok here?
    enum {
        /**
         * Window (or at least part of it) will be painted opaque.
         */
        PAINT_WINDOW_OPAQUE = 1 << 0,
        /**
         * Window (or at least part of it) will be painted translucent.
         */
        PAINT_WINDOW_TRANSLUCENT = 1 << 1,
        /**
         * Window will be painted with transformed geometry.
         */
        PAINT_WINDOW_TRANSFORMED = 1 << 2,
        /**
         * Paint only a region of the screen (can be optimized, cannot
         * be used together with TRANSFORMED flags).
         */
        PAINT_SCREEN_REGION = 1 << 3,
        /**
         * The whole screen will be painted with transformed geometry.
         * Forces the entire screen to be painted.
         */
        PAINT_SCREEN_TRANSFORMED = 1 << 4,
        /**
         * At least one window will be painted with transformed geometry.
         * Forces the entire screen to be painted.
         */
        PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS = 1 << 5,
        /**
         * Clear whole background as the very first step, without optimizing it
         */
        PAINT_SCREEN_BACKGROUND_FIRST = 1 << 6,
        // PAINT_DECORATION_ONLY = 1 << 7 has been deprecated
        /**
         * Window will be painted with a lanczos filter.
         */
        PAINT_WINDOW_LANCZOS = 1 << 8
        // PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_WITHOUT_FULL_REPAINTS = 1 << 9 has been removed
    };

    enum Feature {
        Nothing = 0,
        Resize,
        GeometryTip, /**< @deprecated */
        Outline,     /**< @deprecated */
        ScreenInversion,
        Blur,
        Contrast,
        HighlightWindows
    };

    /**
     * Constructs new Effect object.
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when the Effect is constructed.
     */
    Effect(QObject* parent = nullptr);
    /**
     * Destructs the Effect object.
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when the Effect is destroyed.
     */
    ~Effect() override;

    /**
     * Flags describing which parts of configuration have changed.
     */
    enum ReconfigureFlag {
        ReconfigureAll = 1 << 0 /// Everything needs to be reconfigured.
    };
    Q_DECLARE_FLAGS(ReconfigureFlags, ReconfigureFlag)

    /**
     * Called when configuration changes (either the effect's or KWin's global).
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when the Effect is reconfigured. If this method is called from within the Effect it is
     * required to ensure that the context is current if the implementation does OpenGL calls.
     */
    virtual void reconfigure(ReconfigureFlags flags);

    /**
     * Called when another effect requests the proxy for this effect.
     */
    virtual void* proxy();

    /**
     * Called before starting to paint the screen.
     * In this method you can:
     * @li set whether the windows or the entire screen will be transformed
     * @li change the region of the screen that will be painted
     * @li do various housekeeping tasks such as initing your effect's variables
            for the upcoming paint pass or updating animation's progress
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     *
     * @a presentTime specifies the expected monotonic time when the rendered frame
     * will be displayed on the screen.
    */
    virtual void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime);
    /**
     * In this method you can:
     * @li paint something on top of the windows (by painting after calling
     *      effects->paintScreen())
     * @li paint multiple desktops and/or multiple copies of the same desktop
     *      by calling effects->paintScreen() multiple times
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     */
    virtual void paintScreen(int mask, const QRegion& region, ScreenPaintData& data);
    /**
     * Called after all the painting has been finished.
     * In this method you can:
     * @li schedule next repaint in case of animations
     * You shouldn't paint anything here.
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     */
    virtual void postPaintScreen();

    /**
     * Called for every window before the actual paint pass
     * In this method you can:
     * @li enable or disable painting of the window (e.g. enable paiting of minimized window)
     * @li set window to be painted with translucency
     * @li set window to be transformed
     * @li request the window to be divided into multiple parts
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     *
     * @a presentTime specifies the expected monotonic time when the rendered frame
     * will be displayed on the screen.
     */
    virtual void prePaintWindow(EffectWindow* w,
                                WindowPrePaintData& data,
                                std::chrono::milliseconds presentTime);
    /**
     * This is the main method for painting windows.
     * In this method you can:
     * @li do various transformations
     * @li change opacity of the window
     * @li change brightness and/or saturation, if it's supported
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     */
    virtual void paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data);
    /**
     * Called for every window after all painting has been finished.
     * In this method you can:
     * @li schedule next repaint for individual window(s) in case of animations
     * You shouldn't paint anything here.
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     */
    virtual void postPaintWindow(EffectWindow* w);

    /**
     * Called on Transparent resizes.
     * return true if your effect substitutes questioned feature
     */
    virtual bool provides(Feature);

    /**
     * Performs the @p feature with the @p arguments.
     *
     * This allows to have specific protocols between KWin core and an Effect.
     *
     * The method is supposed to return @c true if it performed the features,
     * @c false otherwise.
     *
     * The default implementation returns @c false.
     * @since 5.8
     */
    virtual bool perform(Feature feature, const QVariantList& arguments);

    /**
     * Can be called to draw multiple copies (e.g. thumbnails) of a window.
     * You can change window's opacity/brightness/etc here, but you can't
     *  do any transformations.
     *
     * In OpenGL based compositing, the frameworks ensures that the context is current
     * when this method is invoked.
     */
    virtual void
    drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data);

    /**
     * Define new window quads so that they can be transformed by other effects.
     * It's up to the effect to keep track of them.
     */
    virtual void buildQuads(EffectWindow* w, WindowQuadList& quadList);

    virtual void windowInputMouseEvent(QEvent* e);
    virtual void grabbedKeyboardEvent(QKeyEvent* e);

    /**
     * Overwrite this method to indicate whether your effect will be doing something in
     * the next frame to be rendered. If the method returns @c false the effect will be
     * excluded from the chained methods in the next rendered frame.
     *
     * This method is called always directly before the paint loop begins. So it is totally
     * fine to e.g. react on a window event, issue a repaint to trigger an animation and
     * change a flag to indicate that this method returns @c true.
     *
     * As the method is called each frame, you should not perform complex calculations.
     * Best use just a boolean flag.
     *
     * The default implementation of this method returns @c true.
     * @since 4.8
     */
    virtual bool isActive() const;

    /**
     * Reimplement this method to provide online debugging.
     * This could be as trivial as printing specific detail information about the effect state
     * but could also be used to move the effect in and out of a special debug modes, clear bogus
     * data, etc.
     * Notice that the functions is const by intent! Whenever you alter the state of the object
     * due to random user input, you should do so with greatest care, hence const_cast<> your
     * object - signalling "let me alone, i know what i'm doing"
     * @param parameter A freeform string user input for your effect to interpret.
     * @since 4.11
     */
    virtual QString debug(const QString& parameter) const;

    /**
     * Reimplement this method to indicate where in the Effect chain the Effect should be placed.
     *
     * A low number indicates early chain position, thus before other Effects got called, a high
     * number indicates a late position. The returned number should be in the interval [0, 100].
     * The default value is 0.
     *
     * In KWin4 this information was provided in the Effect's desktop file as property
     * X-KDE-Ordering. In the case of Scripted Effects this property is still used.
     *
     * @since 5.0
     */
    virtual int requestedEffectChainPosition() const;

    /**
     * A touch point was pressed.
     *
     * If the effect wants to exclusively use the touch event it should return @c true.
     * If @c false is returned the touch event is passed to further effects.
     *
     * In general an Effect should only return @c true if it is the exclusive effect getting
     * input events. E.g. has grabbed mouse events.
     *
     * Default implementation returns @c false.
     *
     * @param id The unique id of the touch point
     * @param pos The position of the touch point in global coordinates
     * @param time Timestamp
     *
     * @see touchMotion
     * @see touchUp
     * @since 5.8
     */
    virtual bool touchDown(qint32 id, const QPointF& pos, quint32 time);
    /**
     * A touch point moved.
     *
     * If the effect wants to exclusively use the touch event it should return @c true.
     * If @c false is returned the touch event is passed to further effects.
     *
     * In general an Effect should only return @c true if it is the exclusive effect getting
     * input events. E.g. has grabbed mouse events.
     *
     * Default implementation returns @c false.
     *
     * @param id The unique id of the touch point
     * @param pos The position of the touch point in global coordinates
     * @param time Timestamp
     *
     * @see touchDown
     * @see touchUp
     * @since 5.8
     */
    virtual bool touchMotion(qint32 id, const QPointF& pos, quint32 time);
    /**
     * A touch point was released.
     *
     * If the effect wants to exclusively use the touch event it should return @c true.
     * If @c false is returned the touch event is passed to further effects.
     *
     * In general an Effect should only return @c true if it is the exclusive effect getting
     * input events. E.g. has grabbed mouse events.
     *
     * Default implementation returns @c false.
     *
     * @param id The unique id of the touch point
     * @param time Timestamp
     *
     * @see touchDown
     * @see touchMotion
     * @since 5.8
     */
    virtual bool touchUp(qint32 id, quint32 time);

    static QPoint cursorPos();

    /**
     * Read animation time from the configuration and possibly adjust using animationTimeFactor().
     * The configuration value in the effect should also have special value 'default' (set using
     * QSpinBox::setSpecialValueText()) with the value 0. This special value is adjusted
     * using the global animation speed, otherwise the exact time configured is returned.
     * @param cfg configuration group to read value from
     * @param key configuration key to read value from
     * @param defaultTime default animation time in milliseconds
     */
    // return type is intentionally double so that one can divide using it without losing data
    static double animationTime(const KConfigGroup& cfg, const QString& key, int defaultTime);
    /**
     * @overload Use this variant if the animation time is hardcoded and not configurable
     * in the effect itself.
     */
    static double animationTime(int defaultTime);
    /**
     * @overload Use this variant if animation time is provided through a KConfigXT generated class
     * having a property called "duration".
     */
    template<typename T>
    int animationTime(int defaultDuration);
    /**
     * Linearly interpolates between @p x and @p y.
     *
     * Returns @p x when @p a = 0; returns @p y when @p a = 1.
     */
    static double interpolate(double x, double y, double a)
    {
        return x * (1 - a) + y * a;
    }
    /** Helper to set WindowPaintData and QRegion to necessary transformations so that
     * a following drawWindow() would put the window at the requested geometry (useful for
     * thumbnails)
     */
    static void setPositionTransformations(WindowPaintData& data,
                                           QRect& region,
                                           EffectWindow* w,
                                           const QRect& r,
                                           Qt::AspectRatioMode aspect);

public Q_SLOTS:
    virtual bool borderActivated(ElectricBorder border);

protected:
    xcb_connection_t* xcbConnection() const;
    xcb_window_t x11RootWindow() const;

    /**
     * An implementing class can call this with it's kconfig compiled singleton class.
     * This method will perform the instance on the class.
     * @since 5.9
     */
    template<typename T>
    void initConfig();
    KSharedConfigPtr get_config();
};

template<typename T>
int Effect::animationTime(int defaultDuration)
{
    return animationTime(T::duration() != 0 ? T::duration() : defaultDuration);
}

template<typename T>
void Effect::initConfig()
{
    T::instance(get_config());
}

}
