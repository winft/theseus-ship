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
#pragma once

// This header is deprecated, but installed for backwards compatibility. Do not include it anymore.
// Instead directly include the headers below.
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

// Below header includes and forward declarations are preserved for backwards compatibility.
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

}
