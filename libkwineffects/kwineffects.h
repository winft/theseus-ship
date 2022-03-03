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
#include <kwineffects/time_line.h>
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

} // namespace

/** @} */

#endif // KWINEFFECTS_H
