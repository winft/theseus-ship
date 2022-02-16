/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#include "screens.h"

#include "base/output.h"
#include "base/platform.h"

#include "input/cursor.h"
#include "win/control.h"
#include "win/screen.h"
#include "win/space.h"
#include "toplevel.h"

namespace KWin
{

Screens::Screens(base::platform const& base)
    : m_maxScale(1.0)
    , base{base}
{
    init();
}

Screens::~Screens()
{
}

void Screens::init()
{
    connect(this, &Screens::sizeChanged, this, &Screens::geometryChanged);
}

QString Screens::name(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->name();
    }
    return QString();
}

QRect Screens::geometry(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->geometry();
    }
    return QRect();
}

QSize Screens::size(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->geometry().size();
    }
    return QSize();
}

float Screens::refreshRate(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->refresh_rate() / 1000.0;
    }
    return 60.0;
}

qreal Screens::maxScale() const
{
    return m_maxScale;
}

qreal Screens::scale(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->scale();
    }
    return 1.0;
}

void Screens::updateAll()
{
    updateSize();
    Q_EMIT changed();
}

void Screens::updateSize()
{
    QRect bounding;
    qreal maxScale = 1.0;
    auto count = base.get_outputs().size();

    for (size_t i = 0; i < count; ++i) {
        bounding = bounding.united(geometry(i));
        maxScale = qMax(maxScale, scale(i));
    }
    if (m_boundingSize != bounding.size()) {
        m_boundingSize = bounding.size();
        Q_EMIT sizeChanged();
    }
    if (!qFuzzyCompare(m_maxScale, maxScale)) {
        m_maxScale = maxScale;
        Q_EMIT maxScaleChanged();
    }
}

int Screens::intersecting(const QRect &r) const
{
    int cnt = 0;
    auto count = base.get_outputs().size();

    for (size_t i = 0; i < count; ++i) {
        if (geometry(i).intersects(r)) {
            ++cnt;
        }
    }
    return cnt;
}

QSize Screens::displaySize() const
{
    return size();
}

QSizeF Screens::physicalSize(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->physical_size();
    }
    return QSizeF(size(screen)) / 3.8;
}

bool Screens::isInternal(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->is_internal();
    }
    return false;
}

Qt::ScreenOrientation Screens::orientation(int screen) const
{
    // TODO: needs implementing
    Q_UNUSED(screen)
    return Qt::PrimaryOrientation;
}

int Screens::physicalDpiX(int screen) const
{
    return size(screen).width() / physicalSize(screen).width() * qreal(25.4);
}

int Screens::physicalDpiY(int screen) const
{
    return size(screen).height() / physicalSize(screen).height() * qreal(25.4);
}

base::output* Screens::findOutput(int screen) const
{
    auto const& outputs = base.get_outputs();
    if (static_cast<int>(outputs.size()) > screen) {
        return outputs.at(screen);
    }
    return nullptr;
}

}
