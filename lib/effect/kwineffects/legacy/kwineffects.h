/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
}
