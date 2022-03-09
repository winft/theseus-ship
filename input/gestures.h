/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QMap>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QVector>

namespace KWin::input
{
static const qreal DEFAULT_MINIMUM_SCALE_DELTA = .2; // 20%
class KWIN_EXPORT gesture : public QObject
{
    Q_OBJECT
public:
    ~gesture() override;

Q_SIGNALS:
    /**
     * Matching of a gesture started and this gesture might match.
     * On further evaluation either the signal @ref triggered or
     * @ref cancelled will get emitted.
     */
    void started();
    /**
     * Gesture matching ended and this gesture matched.
     */
    void triggered();
    /**
     * This gesture no longer matches.
     */
    void cancelled();
};

class KWIN_EXPORT swipe_gesture : public gesture
{
    Q_OBJECT
public:
    enum class Direction { Down, Left, Up, Right };

    ~swipe_gesture() override;

    bool minimumFingerCountIsRelevant() const;
    void setMinimumFingerCount(uint count);
    uint minimumFingerCount() const;

    bool maximumFingerCountIsRelevant() const;
    void setMaximumFingerCount(uint count);
    uint maximumFingerCount() const;

    Direction direction() const;
    void setDirection(Direction direction);

    void setMinimumX(int x);
    int minimumX() const;
    bool minimumXIsRelevant() const;
    void setMinimumY(int y);
    int minimumY() const;
    bool minimumYIsRelevant() const;

    void setMaximumX(int x);
    int maximumX() const;
    bool maximumXIsRelevant() const;
    void setMaximumY(int y);
    int maximumY() const;
    bool maximumYIsRelevant() const;
    void setStartGeometry(const QRect& geometry);

    QSizeF minimumDelta() const;
    void setMinimumDelta(const QSizeF& delta);
    bool isMinimumDeltaRelevant() const;

    qreal minimumDeltaReachedProgress(const QSizeF& delta) const;
    bool minimumDeltaReached(const QSizeF& delta) const;

Q_SIGNALS:
    /**
     * The progress of the gesture if a minimumDelta is set.
     * The progress is reported in [0.0,1.0]
     */
    void progress(qreal);

    /**
     * The progress in actual pixel distance traveled by the fingers
     */
    void deltaProgress(const QSizeF& delta);

private:
    bool m_minimumFingerCountRelevant = false;
    uint m_minimumFingerCount = 0;
    bool m_maximumFingerCountRelevant = false;
    uint m_maximumFingerCount = 0;
    Direction m_direction = Direction::Down;
    bool m_minimumXRelevant = false;
    int m_minimumX = 0;
    bool m_minimumYRelevant = false;
    int m_minimumY = 0;
    bool m_maximumXRelevant = false;
    int m_maximumX = 0;
    bool m_maximumYRelevant = false;
    int m_maximumY = 0;
    bool m_minimumDeltaRelevant = false;
    QSizeF m_minimumDelta;
};

class KWIN_EXPORT pinch_gesture : public gesture
{
    Q_OBJECT
public:
    enum class Direction { Expanding, Contracting };

    ~pinch_gesture() override;

    bool minimumFingerCountIsRelevant() const;
    void setMinimumFingerCount(uint count);
    uint minimumFingerCount() const;

    bool maximumFingerCountIsRelevant() const;
    void setMaximumFingerCount(uint count);
    uint maximumFingerCount() const;

    Direction direction() const;
    void setDirection(Direction direction);

    qreal minimumScaleDelta() const;

    /**
     * scaleDelta is the % scale difference needed to trigger
     * 0.25 will trigger when scale reaches 0.75 or 1.25
     */
    void setMinimumScaleDelta(const qreal& scaleDelta);
    bool isMinimumScaleDeltaRelevant() const;

    qreal minimumScaleDeltaReachedProgress(const qreal& scaleDelta) const;
    bool minimumScaleDeltaReached(const qreal& scaleDelta) const;

Q_SIGNALS:
    /**
     * The progress of the gesture if a minimumDelta is set.
     * The progress is reported in [0.0,1.0]
     */
    void progress(qreal);

private:
    bool m_minimumFingerCountRelevant = false;
    uint m_minimumFingerCount = 0;
    bool m_maximumFingerCountRelevant = false;
    uint m_maximumFingerCount = 0;
    Direction m_direction = Direction::Expanding;
    bool m_minimumScaleDeltaRelevant = false;
    qreal m_minimumScaleDelta = DEFAULT_MINIMUM_SCALE_DELTA;
};

class KWIN_EXPORT gesture_recognizer : public QObject
{
    Q_OBJECT
public:
    ~gesture_recognizer() override;

    void registerSwipeGesture(swipe_gesture* gesture);
    void unregisterSwipeGesture(swipe_gesture* gesture);
    void registerPinchGesture(pinch_gesture* gesture);
    void unregisterPinchGesture(pinch_gesture* gesture);

    int startSwipeGesture(uint fingerCount);
    int startSwipeGesture(const QPointF& startPos);

    void updateSwipeGesture(const QSizeF& delta);
    void cancelSwipeGesture();
    void endSwipeGesture();

    int startPinchGesture(uint fingerCount);
    void updatePinchGesture(qreal scale, qreal angleDelta, const QSizeF& posDelta);
    void cancelPinchGesture();
    void endPinchGesture();

private:
    void cancelActiveGestures();
    enum class StartPositionBehavior { Relevant, Irrelevant };
    enum class Axis {
        Horizontal,
        Vertical,
        None,
    };
    int startSwipeGesture(uint fingerCount,
                          const QPointF& startPos,
                          StartPositionBehavior startPosBehavior);
    QVector<swipe_gesture*> m_swipeGestures;
    QVector<pinch_gesture*> m_pinchGestures;
    QVector<swipe_gesture*> m_activeSwipeGestures;
    QVector<pinch_gesture*> m_activePinchGestures;
    QMap<gesture*, QMetaObject::Connection> m_destroyConnections;

    QSizeF m_currentDelta = QSizeF(0, 0);
    qreal m_currentScale = 1; // For Pinch Gesture recognition
    uint m_currentFingerCount = 0;
    Axis m_currentSwipeAxis = Axis::None;
};

}

Q_DECLARE_METATYPE(KWin::input::swipe_gesture::Direction)
Q_DECLARE_METATYPE(KWin::input::pinch_gesture::Direction)
