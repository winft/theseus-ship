/*
SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "gestures.h"

#include <QRect>
#include <cmath>
#include <functional>

namespace KWin::win
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

qreal swipe_gesture::deltaToProgress(const QSizeF& delta) const
{
    if (!m_minimumDeltaRelevant || m_minimumDelta.isNull()) {
        return 1.0;
    }

    switch (m_direction) {
    case swipe_direction::up:
    case swipe_direction::down:
        return std::min(std::abs(delta.height()) / std::abs(m_minimumDelta.height()), 1.0);
    case swipe_direction::left:
    case swipe_direction::right:
        return std::min(std::abs(delta.width()) / std::abs(m_minimumDelta.width()), 1.0);
    default:
        Q_UNREACHABLE();
    }
}

bool swipe_gesture::minimumDeltaReached(const QSizeF& delta) const
{
    return deltaToProgress(delta) >= 1.0;
}

pinch_gesture::~pinch_gesture() = default;

qreal pinch_gesture::scaleDeltaToProgress(const qreal& scaleDelta) const
{
    return std::clamp(std::abs(scaleDelta - 1) / minimumScaleDelta(), 0.0, 1.0);
}

bool pinch_gesture::minimumScaleDeltaReached(const qreal& scaleDelta) const
{
    return scaleDeltaToProgress(scaleDelta) >= 1.0;
}

gesture_recognizer::~gesture_recognizer() = default;

void gesture_recognizer::registerSwipeGesture(swipe_gesture* gesture)
{
    Q_ASSERT(!m_swipeGestures.contains(gesture));
    auto connection
        = QObject::connect(gesture,
                           &QObject::destroyed,
                           this,
                           std::bind(&gesture_recognizer::unregisterSwipeGesture, this, gesture));
    m_destroyConnections.insert(gesture, connection);
    m_swipeGestures << gesture;
}

void gesture_recognizer::unregisterSwipeGesture(swipe_gesture* gesture)
{
    auto it = m_destroyConnections.find(gesture);
    if (it != m_destroyConnections.end()) {
        QObject::disconnect(it.value());
        m_destroyConnections.erase(it);
    }
    m_swipeGestures.removeAll(gesture);
    if (m_activeSwipeGestures.removeOne(gesture)) {
        Q_EMIT gesture->cancelled();
    }
}

void gesture_recognizer::registerPinchGesture(pinch_gesture* gesture)
{
    Q_ASSERT(!m_pinchGestures.contains(gesture));
    auto connection
        = QObject::connect(gesture,
                           &QObject::destroyed,
                           this,
                           std::bind(&gesture_recognizer::unregisterPinchGesture, this, gesture));
    m_destroyConnections.insert(gesture, connection);
    m_pinchGestures << gesture;
}

void gesture_recognizer::unregisterPinchGesture(pinch_gesture* gesture)
{
    auto it = m_destroyConnections.find(gesture);
    if (it != m_destroyConnections.end()) {
        disconnect(it.value());
        m_destroyConnections.erase(it);
    }
    m_pinchGestures.removeAll(gesture);
    if (m_activePinchGestures.removeOne(gesture)) {
        Q_EMIT gesture->cancelled();
    }
}

int gesture_recognizer::startSwipeGesture(uint fingerCount,
                                          const QPointF& startPos,
                                          StartPositionBehavior startPosBehavior)
{
    m_currentFingerCount = fingerCount;
    if (!m_activeSwipeGestures.isEmpty() || !m_activePinchGestures.isEmpty()) {
        return 0;
    }
    int count = 0;
    for (swipe_gesture* gesture : qAsConst(m_swipeGestures)) {
        if (gesture->minimumFingerCountIsRelevant()) {
            if (gesture->minimumFingerCount() > fingerCount) {
                continue;
            }
        }
        if (gesture->maximumFingerCountIsRelevant()) {
            if (gesture->maximumFingerCount() < fingerCount) {
                continue;
            }
        }
        if (startPosBehavior == StartPositionBehavior::Relevant) {
            if (gesture->minimumXIsRelevant()) {
                if (gesture->minimumX() > startPos.x()) {
                    continue;
                }
            }
            if (gesture->maximumXIsRelevant()) {
                if (gesture->maximumX() < startPos.x()) {
                    continue;
                }
            }
            if (gesture->minimumYIsRelevant()) {
                if (gesture->minimumY() > startPos.y()) {
                    continue;
                }
            }
            if (gesture->maximumYIsRelevant()) {
                if (gesture->maximumY() < startPos.y()) {
                    continue;
                }
            }
        }

        // Only add gestures who's direction aligns with current swipe axis
        switch (gesture->direction()) {
        case swipe_direction::up:
        case swipe_direction::down:
            if (m_currentSwipeAxis == Axis::Horizontal) {
                continue;
            }
            break;
        case swipe_direction::left:
        case swipe_direction::right:
            if (m_currentSwipeAxis == Axis::Vertical) {
                continue;
            }
            break;
        case swipe_direction::invalid:
            continue;
        }

        m_activeSwipeGestures << gesture;
        count++;
        Q_EMIT gesture->started();
    }
    return count;
}

void gesture_recognizer::updateSwipeGesture(const QSizeF& delta)
{
    m_currentDelta += delta;

    swipe_direction direction; // Overall direction
    Axis swipeAxis;

    // Pick an axis for gestures so horizontal ones don't change to vertical ones without lifting
    // fingers
    if (m_currentSwipeAxis == Axis::None) {
        if (std::abs(m_currentDelta.width()) >= std::abs(m_currentDelta.height())) {
            swipeAxis = Axis::Horizontal;
            direction = m_currentDelta.width() < 0 ? swipe_direction::left : swipe_direction::right;
        } else {
            swipeAxis = Axis::Vertical;
            direction = m_currentDelta.height() < 0 ? swipe_direction::up : swipe_direction::down;
        }
        if (std::abs(m_currentDelta.width()) >= 5 || std::abs(m_currentDelta.height()) >= 5) {
            // only lock in a direction if the delta is big enough
            // to prevent accidentally choosing the wrong direction
            m_currentSwipeAxis = swipeAxis;
        }
    } else {
        swipeAxis = m_currentSwipeAxis;
    }

    // Find the current swipe direction
    switch (swipeAxis) {
    case Axis::Vertical:
        direction = m_currentDelta.height() < 0 ? swipe_direction::up : swipe_direction::down;
        break;
    case Axis::Horizontal:
        direction = m_currentDelta.width() < 0 ? swipe_direction::left : swipe_direction::right;
        break;
    default:
        Q_UNREACHABLE();
    }

    // Eliminate wrong gestures (takes two iterations)
    for (int i = 0; i < 2; i++) {

        if (m_activeSwipeGestures.isEmpty()) {
            startSwipeGesture(m_currentFingerCount);
        }

        for (auto it = m_activeSwipeGestures.begin(); it != m_activeSwipeGestures.end();) {
            auto g = static_cast<swipe_gesture*>(*it);

            if (g->direction() != direction) {
                // If a gesture was started from a touchscreen border never cancel it
                if (!g->minimumXIsRelevant() || !g->maximumXIsRelevant() || !g->minimumYIsRelevant()
                    || !g->maximumYIsRelevant()) {
                    Q_EMIT g->cancelled();
                    it = m_activeSwipeGestures.erase(it);
                    continue;
                }
            }

            it++;
        }
    }

    // Send progress update
    for (swipe_gesture* g : std::as_const(m_activeSwipeGestures)) {
        Q_EMIT g->progress(g->deltaToProgress(m_currentDelta));
        Q_EMIT g->deltaProgress(m_currentDelta);
    }
}

void gesture_recognizer::cancelActiveGestures()
{
    for (auto g : qAsConst(m_activeSwipeGestures)) {
        Q_EMIT g->cancelled();
    }
    for (auto g : qAsConst(m_activePinchGestures)) {
        Q_EMIT g->cancelled();
    }
    m_activeSwipeGestures.clear();
    m_activePinchGestures.clear();
    m_currentScale = 0;
    m_currentDelta = QSizeF(0, 0);
    m_currentSwipeAxis = Axis::None;
}

void gesture_recognizer::cancelSwipeGesture()
{
    cancelActiveGestures();
    m_currentFingerCount = 0;
    m_currentDelta = QSizeF(0, 0);
    m_currentSwipeAxis = Axis::None;
}

void gesture_recognizer::endSwipeGesture()
{
    const QSizeF delta = m_currentDelta;
    for (auto g : qAsConst(m_activeSwipeGestures)) {
        if (static_cast<swipe_gesture*>(g)->minimumDeltaReached(delta)) {
            Q_EMIT g->triggered();
        } else {
            Q_EMIT g->cancelled();
        }
    }
    m_activeSwipeGestures.clear();
    m_currentFingerCount = 0;
    m_currentDelta = QSizeF(0, 0);
    m_currentSwipeAxis = Axis::None;
}

int gesture_recognizer::startPinchGesture(uint fingerCount)
{
    m_currentFingerCount = fingerCount;
    int count = 0;
    if (!m_activeSwipeGestures.isEmpty() || !m_activePinchGestures.isEmpty()) {
        return 0;
    }
    for (pinch_gesture* gesture : qAsConst(m_pinchGestures)) {
        if (gesture->minimumFingerCountIsRelevant()) {
            if (gesture->minimumFingerCount() > fingerCount) {
                continue;
            }
        }
        if (gesture->maximumFingerCountIsRelevant()) {
            if (gesture->maximumFingerCount() < fingerCount) {
                continue;
            }
        }

        // direction doesn't matter yet
        m_activePinchGestures << gesture;
        count++;
        Q_EMIT gesture->started();
    }
    return count;
}

void gesture_recognizer::updatePinchGesture(qreal scale, qreal angleDelta, const QSizeF& posDelta)
{
    Q_UNUSED(angleDelta);
    Q_UNUSED(posDelta);
    m_currentScale = scale;

    // Determine the direction of the swipe
    pinch_direction direction;
    if (scale < 1) {
        direction = pinch_direction::contracting;
    } else {
        direction = pinch_direction::expanding;
    }

    // Eliminate wrong gestures (takes two iterations)
    for (int i = 0; i < 2; i++) {
        if (m_activePinchGestures.isEmpty()) {
            startPinchGesture(m_currentFingerCount);
        }

        for (auto it = m_activePinchGestures.begin(); it != m_activePinchGestures.end();) {
            auto g = static_cast<pinch_gesture*>(*it);

            if (g->direction() != direction) {
                Q_EMIT g->cancelled();
                it = m_activePinchGestures.erase(it);
                continue;
            }
            it++;
        }
    }

    for (pinch_gesture* g : std::as_const(m_activePinchGestures)) {
        Q_EMIT g->progress(g->scaleDeltaToProgress(scale));
    }
}

void gesture_recognizer::cancelPinchGesture()
{
    cancelActiveGestures();
    m_currentScale = 1;
    m_currentFingerCount = 0;
    m_currentSwipeAxis = Axis::None;
}

void gesture_recognizer::endPinchGesture() // because fingers up
{
    for (auto g : qAsConst(m_activePinchGestures)) {
        if (g->minimumScaleDeltaReached(m_currentScale)) {
            Q_EMIT g->triggered();
        } else {
            Q_EMIT g->cancelled();
        }
    }
    m_activeSwipeGestures.clear();
    m_activePinchGestures.clear();
    m_currentScale = 1;
    m_currentFingerCount = 0;
    m_currentSwipeAxis = Axis::None;
}

bool swipe_gesture::maximumFingerCountIsRelevant() const
{
    return m_maximumFingerCountRelevant;
}

uint swipe_gesture::minimumFingerCount() const
{
    return m_minimumFingerCount;
}

void swipe_gesture::setMinimumFingerCount(uint count)
{
    m_minimumFingerCount = count;
    m_minimumFingerCountRelevant = true;
}

bool swipe_gesture::minimumFingerCountIsRelevant() const
{
    return m_minimumFingerCountRelevant;
}

void swipe_gesture::setMaximumFingerCount(uint count)
{
    m_maximumFingerCount = count;
    m_maximumFingerCountRelevant = true;
}

uint swipe_gesture::maximumFingerCount() const
{
    return m_maximumFingerCount;
}

swipe_direction swipe_gesture::direction() const
{
    return m_direction;
}

void swipe_gesture::setDirection(swipe_direction direction)
{
    m_direction = direction;
}

void swipe_gesture::setMinimumX(int x)
{
    m_minimumX = x;
    m_minimumXRelevant = true;
}

int swipe_gesture::minimumX() const
{
    return m_minimumX;
}

bool swipe_gesture::minimumXIsRelevant() const
{
    return m_minimumXRelevant;
}

void swipe_gesture::setMinimumY(int y)
{
    m_minimumY = y;
    m_minimumYRelevant = true;
}

int swipe_gesture::minimumY() const
{
    return m_minimumY;
}

bool swipe_gesture::minimumYIsRelevant() const
{
    return m_minimumYRelevant;
}

void swipe_gesture::setMaximumX(int x)
{
    m_maximumX = x;
    m_maximumXRelevant = true;
}

int swipe_gesture::maximumX() const
{
    return m_maximumX;
}

bool swipe_gesture::maximumXIsRelevant() const
{
    return m_maximumXRelevant;
}

void swipe_gesture::setMaximumY(int y)
{
    m_maximumY = y;
    m_maximumYRelevant = true;
}

int swipe_gesture::maximumY() const
{
    return m_maximumY;
}

bool swipe_gesture::maximumYIsRelevant() const
{
    return m_maximumYRelevant;
}

QSizeF swipe_gesture::minimumDelta() const
{
    return m_minimumDelta;
}

void swipe_gesture::setMinimumDelta(const QSizeF& delta)
{
    m_minimumDelta = delta;
    m_minimumDeltaRelevant = true;
}

bool swipe_gesture::isMinimumDeltaRelevant() const
{
    return m_minimumDeltaRelevant;
}

bool pinch_gesture::minimumFingerCountIsRelevant() const
{
    return m_minimumFingerCountRelevant;
}

void pinch_gesture::setMinimumFingerCount(uint count)
{
    m_minimumFingerCount = count;
    m_minimumFingerCountRelevant = true;
}

uint pinch_gesture::minimumFingerCount() const
{
    return m_minimumFingerCount;
}

bool pinch_gesture::maximumFingerCountIsRelevant() const
{
    return m_maximumFingerCountRelevant;
}

void pinch_gesture::setMaximumFingerCount(uint count)
{
    m_maximumFingerCount = count;
    m_maximumFingerCountRelevant = true;
}

uint pinch_gesture::maximumFingerCount() const
{
    return m_maximumFingerCount;
}

pinch_direction pinch_gesture::direction() const
{
    return m_direction;
}

void pinch_gesture::setDirection(pinch_direction direction)
{
    m_direction = direction;
}

qreal pinch_gesture::minimumScaleDelta() const
{
    return m_minimumScaleDelta;
}

void pinch_gesture::setMinimumScaleDelta(const qreal& scaleDelta)
{
    m_minimumScaleDelta = scaleDelta;
    m_minimumScaleDeltaRelevant = true;
}

bool pinch_gesture::isMinimumScaleDeltaRelevant() const
{
    return m_minimumScaleDeltaRelevant;
}

int gesture_recognizer::startSwipeGesture(uint fingerCount)
{
    return startSwipeGesture(fingerCount, QPointF(), StartPositionBehavior::Irrelevant);
}

int gesture_recognizer::startSwipeGesture(const QPointF& startPos)
{
    return startSwipeGesture(1, startPos, StartPositionBehavior::Relevant);
}

}
