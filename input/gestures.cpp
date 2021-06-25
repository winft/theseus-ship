/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Gräßlin <mgraesslin@kde.org>

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
#include "gestures.h"

#include <QRect>
#include <cmath>
#include <functional>

namespace KWin::input
{

gesture::~gesture() = default;

swipe_gesture::~swipe_gesture() = default;

void swipe_gesture::setStartGeometry(const QRect& geometry)
{
    setMinimumX(geometry.x());
    setMinimumY(geometry.y());
    setMaximumX(geometry.x() + geometry.width());
    setMaximumY(geometry.y() + geometry.height());

    Q_ASSERT(m_maximumX >= m_minimumX);
    Q_ASSERT(m_maximumY >= m_minimumY);
}

qreal swipe_gesture::minimumDeltaReachedProgress(const QSizeF& delta) const
{
    if (!m_minimumDeltaRelevant || m_minimumDelta.isNull()) {
        return 1.0;
    }
    switch (m_direction) {
    case Direction::Up:
    case Direction::Down:
        return std::min(std::abs(delta.height()) / std::abs(m_minimumDelta.height()), 1.0);
    case Direction::Left:
    case Direction::Right:
        return std::min(std::abs(delta.width()) / std::abs(m_minimumDelta.width()), 1.0);
    default:
        Q_UNREACHABLE();
    }
}

bool swipe_gesture::minimumDeltaReached(const QSizeF& delta) const
{
    return minimumDeltaReachedProgress(delta) >= 1.0;
}

gesture_recognizer::~gesture_recognizer() = default;

void gesture_recognizer::registerGesture(gesture* gesture)
{
    Q_ASSERT(!m_gestures.contains(gesture));
    auto connection
        = QObject::connect(gesture,
                           &QObject::destroyed,
                           this,
                           std::bind(&gesture_recognizer::unregisterGesture, this, gesture));
    m_destroyConnections.insert(gesture, connection);
    m_gestures << gesture;
}

void gesture_recognizer::unregisterGesture(gesture* gesture)
{
    auto it = m_destroyConnections.find(gesture);
    if (it != m_destroyConnections.end()) {
        QObject::disconnect(it.value());
        m_destroyConnections.erase(it);
    }
    m_gestures.removeAll(gesture);
    if (m_activeSwipeGestures.removeOne(gesture)) {
        Q_EMIT gesture->cancelled();
    }
}

int gesture_recognizer::startSwipeGesture(uint fingerCount,
                                          const QPointF& startPos,
                                          StartPositionBehavior startPosBehavior)
{
    int count = 0;
    // TODO: verify that no gesture is running
    for (auto gesture : qAsConst(m_gestures)) {
        auto swipeGesture = qobject_cast<swipe_gesture*>(gesture);
        if (!gesture) {
            continue;
        }
        if (swipeGesture->minimumFingerCountIsRelevant()) {
            if (swipeGesture->minimumFingerCount() > fingerCount) {
                continue;
            }
        }
        if (swipeGesture->maximumFingerCountIsRelevant()) {
            if (swipeGesture->maximumFingerCount() < fingerCount) {
                continue;
            }
        }
        if (startPosBehavior == StartPositionBehavior::Relevant) {
            if (swipeGesture->minimumXIsRelevant()) {
                if (swipeGesture->minimumX() > startPos.x()) {
                    continue;
                }
            }
            if (swipeGesture->maximumXIsRelevant()) {
                if (swipeGesture->maximumX() < startPos.x()) {
                    continue;
                }
            }
            if (swipeGesture->minimumYIsRelevant()) {
                if (swipeGesture->minimumY() > startPos.y()) {
                    continue;
                }
            }
            if (swipeGesture->maximumYIsRelevant()) {
                if (swipeGesture->maximumY() < startPos.y()) {
                    continue;
                }
            }
        }
        // direction doesn't matter yet
        m_activeSwipeGestures << swipeGesture;
        count++;
        Q_EMIT swipeGesture->started();
    }
    return count;
}

void gesture_recognizer::updateSwipeGesture(const QSizeF& delta)
{
    m_swipeUpdates << delta;
    m_currentDelta += delta;
    // with high resolution touch(pad) gestures can be cancelled without intention
    // -> don't cancel movements if their accumulated values are too small but also still update the
    // gesture for animations
    if (std::abs(m_currentDelta.width()) > 1 || std::abs(m_currentDelta.height()) > 1) {
        m_lastDelta = m_currentDelta;
        m_currentDelta = QSizeF(0, 0);
    } else if (std::abs(m_lastDelta.width()) < 1 && std::abs(m_lastDelta.height()) < 1) {
        // no direction yet
        return;
    }
    // determine the direction of the swipe
    if (m_lastDelta.width() == m_lastDelta.height()) {
        // special case of diagonal, this is not yet supported, thus cancel all gestures
        cancelActiveSwipeGestures();
        return;
    }
    swipe_gesture::Direction direction;
    if (std::abs(m_lastDelta.width()) > std::abs(m_lastDelta.height())) {
        // horizontal
        direction = m_lastDelta.width() < 0 ? swipe_gesture::Direction::Left
                                            : swipe_gesture::Direction::Right;
    } else {
        // vertical
        direction = m_lastDelta.height() < 0 ? swipe_gesture::Direction::Up
                                             : swipe_gesture::Direction::Down;
    }
    const QSizeF combinedDelta
        = std::accumulate(m_swipeUpdates.constBegin(), m_swipeUpdates.constEnd(), QSizeF(0, 0));
    for (auto it = m_activeSwipeGestures.begin(); it != m_activeSwipeGestures.end();) {
        auto g = qobject_cast<swipe_gesture*>(*it);
        if (g->direction() == direction) {
            if (g->isMinimumDeltaRelevant()) {
                Q_EMIT g->progress(g->minimumDeltaReachedProgress(combinedDelta));
            }
            it++;
        } else {
            Q_EMIT g->cancelled();
            it = m_activeSwipeGestures.erase(it);
        }
    }
}

void gesture_recognizer::cancelActiveSwipeGestures()
{
    for (auto g : qAsConst(m_activeSwipeGestures)) {
        Q_EMIT g->cancelled();
    }
    m_activeSwipeGestures.clear();
    m_currentDelta = QSizeF(0, 0);
    m_lastDelta = QSizeF(0, 0);
}

void gesture_recognizer::cancelSwipeGesture()
{
    cancelActiveSwipeGestures();
    m_swipeUpdates.clear();
    m_currentDelta = QSizeF(0, 0);
    m_lastDelta = QSizeF(0, 0);
}

void gesture_recognizer::endSwipeGesture()
{
    const QSizeF delta
        = std::accumulate(m_swipeUpdates.constBegin(), m_swipeUpdates.constEnd(), QSizeF(0, 0));
    for (auto g : qAsConst(m_activeSwipeGestures)) {
        if (static_cast<swipe_gesture*>(g)->minimumDeltaReached(delta)) {
            Q_EMIT g->triggered();
        } else {
            Q_EMIT g->cancelled();
        }
    }
    m_activeSwipeGestures.clear();
    m_swipeUpdates.clear();
    m_currentDelta = QSizeF(0, 0);
    m_lastDelta = QSizeF(0, 0);
}

}
