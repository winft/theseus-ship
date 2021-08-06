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
    static const QMap<SwipeDirection, swipe_gesture::Direction> dirs = {
        {SwipeDirection::Up, swipe_gesture::Direction::Up},
        {SwipeDirection::Down, swipe_gesture::Direction::Down},
        {SwipeDirection::Left, swipe_gesture::Direction::Left},
        {SwipeDirection::Right, swipe_gesture::Direction::Right},
    };
    if (auto swipeGesture = std::get_if<FourFingerSwipeShortcut>(&sc)) {
        m_gesture.reset(new swipe_gesture);
        m_gesture->setDirection(dirs[swipeGesture->swipeDirection]);
        m_gesture->setMaximumFingerCount(4);
        m_gesture->setMinimumFingerCount(4);
        QObject::connect(m_gesture.get(),
                         &swipe_gesture::triggered,
                         m_action,
                         &QAction::trigger,
                         Qt::QueuedConnection);
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
    return m_gesture.get();
}

}
