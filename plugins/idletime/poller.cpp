/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2022 Roman Gilg <subdiff@gmail.com>

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
#include "poller.h"

#include "input/idle.h"
#include "input/singleton_interface.h"

#include <algorithm>

KWinIdleTimePoller::KWinIdleTimePoller(QObject *parent)
    : AbstractSystemPoller(parent)
{
}

KWinIdleTimePoller::~KWinIdleTimePoller()
{
    cleanup();
}

void KWinIdleTimePoller::cleanup()
{
    if (auto idle_interface = KWin::input::singleton_interface::idle_qobject) {
        for (auto& listener : qAsConst(m_timeouts)) {
            idle_interface->unregister_listener(*listener);
        }
        if (m_catchResumeTimeout) {
            idle_interface->unregister_listener(*m_catchResumeTimeout);
        }
    }

    qDeleteAll(m_timeouts);
    m_timeouts.clear();

    delete m_catchResumeTimeout;
    m_catchResumeTimeout = nullptr;

}

bool KWinIdleTimePoller::isAvailable()
{
    return true;
}

bool KWinIdleTimePoller::setUpPoller()
{
    auto idle_interface = KWin::input::singleton_interface::idle_qobject;
    if (!idle_interface) {
        return false;
    }

    QObject::connect(idle_interface, &QObject::destroyed, this, &KWinIdleTimePoller::cleanup);
    return true;
}

void KWinIdleTimePoller::unloadPoller()
{
    cleanup();
}

void KWinIdleTimePoller::addTimeout(int nextTimeout)
{
    using namespace KWin;

    nextTimeout = std::max(0, nextTimeout);
    if (m_timeouts.contains(nextTimeout)) {
        return;
    }

    auto idle_interface = input::singleton_interface::idle_qobject;
    if (!idle_interface) {
        return;
    }

    auto listener
        = new input::idle_listener({std::chrono::milliseconds(nextTimeout),
                                    [this, nextTimeout] { Q_EMIT timeoutReached(nextTimeout); },
                                    [this] { Q_EMIT resumingFromIdle(); }});

    idle_interface->register_listener(*listener);
    m_timeouts.insert(nextTimeout, listener);
}

void KWinIdleTimePoller::removeTimeout(int nextTimeout)
{
    auto it = m_timeouts.find(nextTimeout);
    if (it == m_timeouts.end()) {
        return;
    }

    if (auto idle_interface = KWin::input::singleton_interface::idle_qobject) {
        idle_interface->unregister_listener(*it.value());
    }

    delete it.value();
    m_timeouts.erase(it);
}

QList< int > KWinIdleTimePoller::timeouts() const
{
    return QList<int>();
}

void KWinIdleTimePoller::catchIdleEvent()
{
    if (m_catchResumeTimeout) {
        // already setup
        return;
    }

    auto idle_interface = KWin::input::singleton_interface::idle_qobject;
    if (!idle_interface) {
        return;
    }

    m_catchResumeTimeout = new KWin::input::idle_listener({{}, {}, [this] {
                                                               stopCatchingIdleEvents();
                                                               Q_EMIT resumingFromIdle();
                                                           }});

    idle_interface->register_listener(*m_catchResumeTimeout);
}

void KWinIdleTimePoller::stopCatchingIdleEvents()
{
    if (auto idle_interface = KWin::input::singleton_interface::idle_qobject) {
        idle_interface->unregister_listener(*m_catchResumeTimeout);
    }
    delete m_catchResumeTimeout;
    m_catchResumeTimeout = nullptr;
}

int KWinIdleTimePoller::forcePollRequest()
{
    return 0;
}

void KWinIdleTimePoller::simulateUserActivity()
{
    if (auto idle_interface = KWin::input::singleton_interface::idle_qobject) {
        idle_interface->simulate_activity();
    }
}
