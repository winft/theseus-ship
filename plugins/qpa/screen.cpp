/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen.h"

#include "integration.h"

#include "base/output_helpers.h"
#include "platformcursor.h"

namespace KWin
{
namespace QPA
{

Screen::Screen(base::output* output, Integration* integration)
    : QPlatformScreen()
    , output{output}
    , m_cursor(new PlatformCursor)
    , m_integration(integration)
{
}

Screen::~Screen() = default;

QList<QPlatformScreen*> Screen::virtualSiblings() const
{
    auto const screens = m_integration->screens();

    QList<QPlatformScreen*> siblings;
    siblings.reserve(siblings.size());

    for (auto screen : screens) {
        siblings << screen;
    }

    return siblings;
}

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

QPlatformCursor* Screen::cursor() const
{
    return m_cursor.data();
}

QDpi Screen::logicalDpi() const
{
    static int forceDpi = qEnvironmentVariableIsSet("QT_WAYLAND_FORCE_DPI")
        ? qEnvironmentVariableIntValue("QT_WAYLAND_FORCE_DPI")
        : -1;
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
