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

#include "abstract_output.h"

#include <abstract_client.h>
#include <x11client.h>
#include "cursor.h"
#include "utils.h"
#include "settings.h"
#include <workspace.h>
#include <config-kwin.h>
#include "platform.h"
#include "wayland_server.h"

namespace KWin
{

Screens *Screens::s_self = nullptr;
Screens *Screens::create(QObject *parent)
{
    Q_ASSERT(!s_self);
    s_self = new Screens(parent);
    Q_ASSERT(s_self);
    s_self->init();
    return s_self;
}

Screens::Screens(QObject *parent)
    : QObject(parent)
    , m_count(0)
    , m_current(0)
    , m_currentFollowsMouse(false)
    , m_changedTimer(new QTimer(this))
    , m_maxScale(1.0)
{
}

Screens::~Screens()
{
    s_self = nullptr;
}

void Screens::init()
{
    m_changedTimer->setSingleShot(true);
    m_changedTimer->setInterval(100);
    connect(m_changedTimer, &QTimer::timeout, this, &Screens::updateCount);
    connect(m_changedTimer, SIGNAL(timeout()), SIGNAL(changed()));
    connect(this, &Screens::countChanged, this, &Screens::changed, Qt::QueuedConnection);
    connect(this, &Screens::changed, this, &Screens::updateSize);
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
        return output->refreshRate() / 1000.0;
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
        emit sizeChanged();
    }
    if (!qFuzzyCompare(m_maxScale, maxScale)) {
        m_maxScale = maxScale;
        emit maxScaleChanged();
    }
}

void Screens::updateCount()
{
    setCount(kwinApp()->platform()->enabledOutputs().size());
}

void Screens::setCount(int count)
{
    if (m_count == count) {
        return;
    }
    const int previous = m_count;
    m_count = count;
    emit countChanged(previous, count);
}

void Screens::setCurrent(int current)
{
    if (m_current == current) {
        return;
    }
    m_current = current;
    emit currentChanged();
}

void Screens::setCurrent(const QPoint &pos)
{
    setCurrent(number(pos));
}

void Screens::setCurrent(const AbstractClient *c)
{
    if (!c->isActive()) {
        return;
    }
    if (!c->isOnScreen(m_current)) {
        setCurrent(c->screen());
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
        return number(Cursor::pos());
    }
    AbstractClient *client = Workspace::self()->activeClient();
    if (client && !client->isOnScreen(m_current)) {
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
        return output->physicalSize();
    }
    return QSizeF(size(screen)) / 3.8;
}

bool Screens::isInternal(int screen) const
{
    if (auto output = findOutput(screen)) {
        return output->isInternal();
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

AbstractOutput *Screens::findOutput(int screen) const
{
    return kwinApp()->platform()->enabledOutputs().value(screen);
}

int Screens::number(const QPoint &pos) const
{
    int bestScreen = 0;
    int minDistance = INT_MAX;
    const auto outputs = kwinApp()->platform()->enabledOutputs();
    for (int i = 0; i < outputs.size(); ++i) {
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
