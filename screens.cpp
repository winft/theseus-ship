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
#include "settings.h"
#include "win/control.h"
#include "win/screen.h"
#include <workspace.h>
#include "toplevel.h"

namespace KWin
{

Screens::Screens(base::platform const& base)
    : m_count(0)
    , m_current(0)
    , m_currentFollowsMouse(false)
    , m_maxScale(1.0)
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

    Settings settings;
    settings.setDefaults();
    m_currentFollowsMouse = settings.activeMouseScreen();
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

void Screens::reconfigure()
{
    if (!m_config) {
        return;
    }
    Settings settings(m_config);
    settings.read();
    setCurrentFollowsMouse(settings.activeMouseScreen());
}

void Screens::updateAll()
{
    updateCount();
    updateSize();
    Q_EMIT changed();
}

void Screens::updateSize()
{
    QRect bounding;
    qreal maxScale = 1.0;
    for (int i = 0; i < count(); ++i) {
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

void Screens::updateCount()
{
    auto size = base.get_outputs().size();
    setCount(size);
}

void Screens::setCount(int count)
{
    if (m_count == count) {
        return;
    }
    const int previous = m_count;
    m_count = count;
    Q_EMIT countChanged(previous, count);
}

void Screens::setCurrent(int current)
{
    if (m_current == current) {
        return;
    }
    m_current = current;
    Q_EMIT currentChanged();
}

void Screens::setCurrent(const QPoint &pos)
{
    setCurrent(number(pos));
}

void Screens::setCurrent(Toplevel const* window)
{
    if (!window->control->active()) {
        return;
    }
    if (!win::on_screen(window, m_current)) {
        setCurrent(window->screen());
    }
}

void Screens::setCurrentFollowsMouse(bool follows)
{
    if (m_currentFollowsMouse == follows) {
        return;
    }
    m_currentFollowsMouse = follows;
}

int Screens::current() const
{
    if (m_currentFollowsMouse) {
        return number(input::get_cursor()->pos());
    }
    auto client = workspace()->activeClient();
    if (client && !win::on_screen(client, m_current)) {
        return client->screen();
    }
    return m_current;
}

int Screens::intersecting(const QRect &r) const
{
    int cnt = 0;
    for (int i = 0; i < count(); ++i) {
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

void Screens::setConfig(KSharedConfig::Ptr config)
{
    m_config = config;
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

int Screens::number(const QPoint &pos) const
{
    int bestScreen = 0;
    int minDistance = INT_MAX;
    auto const outputs = base.get_outputs();
    for (size_t i = 0; i < outputs.size(); ++i) {
        const QRect &geo = outputs[i]->geometry();
        if (geo.contains(pos)) {
            return i;
        }
        int distance = QPoint(geo.topLeft() - pos).manhattanLength();
        distance = qMin(distance, QPoint(geo.topRight() - pos).manhattanLength());
        distance = qMin(distance, QPoint(geo.bottomRight() - pos).manhattanLength());
        distance = qMin(distance, QPoint(geo.bottomLeft() - pos).manhattanLength());
        if (distance < minDistance) {
            minDistance = distance;
            bestScreen = i;
        }
    }
    return bestScreen;
}

}
