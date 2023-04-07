/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "poller.h"

#include "input/idle.h"
#include "input/singleton_interface.h"

#include <algorithm>

KWinIdleTimePoller::KWinIdleTimePoller(QObject *parent)
    : KAbstractIdleTimePoller(parent)
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
