/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcut.h"

#include "gestures.h"

#include <QAction>
#include <variant>

namespace KWin::input
{

global_shortcut::global_shortcut(Shortcut&& sc, QAction* action)
    : m_shortcut(sc)
    , m_action(action)
{
    static const QMap<SwipeDirection, SwipeDirection> swipeDirs = {
        {SwipeDirection::Up, SwipeDirection::Up},
        {SwipeDirection::Down, SwipeDirection::Down},
        {SwipeDirection::Left, SwipeDirection::Left},
        {SwipeDirection::Right, SwipeDirection::Right},
    };
    static const QMap<PinchDirection, PinchDirection> pinchDirs = {
        {PinchDirection::Expanding, PinchDirection::Expanding},
        {PinchDirection::Contracting, PinchDirection::Contracting},
    };

    if (auto swipeGesture = std::get_if<RealtimeFeedbackSwipeShortcut>(&sc)) {
        m_swipeGesture.reset(new swipe_gesture());
        m_swipeGesture->setDirection(swipeDirs[swipeGesture->direction]);
        m_swipeGesture->setMinimumDelta(QSizeF(200, 200));
        m_swipeGesture->setMaximumFingerCount(swipeGesture->fingerCount);
        m_swipeGesture->setMinimumFingerCount(swipeGesture->fingerCount);
        QObject::connect(m_swipeGesture.get(),
                         &swipe_gesture::triggered,
                         m_action,
                         &QAction::trigger,
                         Qt::QueuedConnection);
        QObject::connect(m_swipeGesture.get(),
                         &swipe_gesture::cancelled,
                         m_action,
                         &QAction::trigger,
                         Qt::QueuedConnection);
        QObject::connect(m_swipeGesture.get(),
                         &swipe_gesture::progress,
                         [cb = swipeGesture->progressCallback](qreal v) {
                             if (cb) {
                                 cb(v);
                             }
                         });
    } else if (auto pinchGesture = std::get_if<RealtimeFeedbackPinchShortcut>(&sc)) {
        m_pinchGesture.reset(new pinch_gesture());
        m_pinchGesture->setDirection(pinchDirs[pinchGesture->direction]);
        m_pinchGesture->setMaximumFingerCount(pinchGesture->fingerCount);
        m_pinchGesture->setMinimumFingerCount(pinchGesture->fingerCount);
        QObject::connect(m_pinchGesture.get(),
                         &pinch_gesture::triggered,
                         m_action,
                         &QAction::trigger,
                         Qt::QueuedConnection);
        QObject::connect(m_pinchGesture.get(),
                         &pinch_gesture::cancelled,
                         m_action,
                         &QAction::trigger,
                         Qt::QueuedConnection);
        QObject::connect(m_pinchGesture.get(),
                         &pinch_gesture::progress,
                         [cb = pinchGesture->scaleCallback](qreal v) {
                             if (cb) {
                                 cb(v);
                             }
                         });
    }
}

global_shortcut::~global_shortcut()
{
}

QAction* global_shortcut::action() const
{
    return m_action;
}

void global_shortcut::invoke() const
{
    QMetaObject::invokeMethod(m_action, "trigger", Qt::QueuedConnection);
}

Shortcut const& global_shortcut::shortcut() const
{
    return m_shortcut;
}

swipe_gesture* global_shortcut::swipeGesture() const
{
    return m_swipeGesture.get();
}

pinch_gesture* global_shortcut::pinchGesture() const
{
    return m_pinchGesture.get();
}

}
