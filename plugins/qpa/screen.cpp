/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "screen.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "platformcursor.h"

namespace KWin
{
namespace QPA
{

Screen::Screen(base::output* output)
    : QPlatformScreen()
    , output{output}
    , m_cursor(new PlatformCursor)
{
}

Screen::~Screen() = default;

int Screen::depth() const
{
    return 32;
}

QImage::Format Screen::format() const
{
    return QImage::Format_ARGB32_Premultiplied;
}

QRect Screen::geometry() const
{
    return output ? output->geometry() : QRect(0, 0, 1, 1);
}

QSizeF Screen::physicalSize() const
{
    return output ? output->physical_size() : QPlatformScreen::physicalSize();
}

QPlatformCursor *Screen::cursor() const
{
    return m_cursor.data();
}

QDpi Screen::logicalDpi() const
{
    static int forceDpi = qEnvironmentVariableIsSet("QT_WAYLAND_FORCE_DPI") ? qEnvironmentVariableIntValue("QT_WAYLAND_FORCE_DPI") : -1;
    if (forceDpi > 0) {
        return QDpi(forceDpi, forceDpi);
    }

    return QDpi(96, 96);
}

qreal Screen::devicePixelRatio() const
{
    return output ? output->scale() : 1.;
}

QString Screen::name() const
{
    return output ? output->name() : QString();
}

}
}
