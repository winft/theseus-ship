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
#include <kwineffects/effect_frame.h>
#include <kwineffects/effect_plugin_factory.h>
#include <kwineffects/effect_screen.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/motions.h>
#include <kwineffects/paint_clipper.h>
#include <kwineffects/paint_data.h>
#include <kwineffects/types.h>
#include <kwineffects/window_quad.h>

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

#include <climits>
#include <functional>

class KConfigGroup;
class QFont;
class QMatrix4x4;

namespace KWin
{

class XRenderPicture;

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

} // namespace
Q_DECLARE_METATYPE(KWin::TimeLine)
Q_DECLARE_METATYPE(KWin::TimeLine::Direction)

/** @} */

#endif // KWINEFFECTS_H
