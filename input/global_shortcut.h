/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <QKeySequence>
#include <QSharedPointer>

class QAction;

namespace KWin::input
{
class swipe_gesture;

struct KeyboardShortcut {
    QKeySequence sequence;
    bool operator==(const KeyboardShortcut& rhs) const
    {
        return sequence == rhs.sequence;
    }
};

struct PointerButtonShortcut {
    Qt::KeyboardModifiers pointerModifiers;
    Qt::MouseButtons pointerButtons;
    bool operator==(const PointerButtonShortcut& rhs) const
    {
        return pointerModifiers == rhs.pointerModifiers && pointerButtons == rhs.pointerButtons;
    }
};

struct PointerAxisShortcut {
    Qt::KeyboardModifiers axisModifiers;
    PointerAxisDirection axisDirection;
    bool operator==(const PointerAxisShortcut& rhs) const
    {
        return axisModifiers == rhs.axisModifiers && axisDirection == rhs.axisDirection;
    }
};

struct FourFingerSwipeShortcut {
    SwipeDirection swipeDirection;
    bool operator==(const FourFingerSwipeShortcut& rhs) const
    {
        return swipeDirection == rhs.swipeDirection;
    }
};

using Shortcut = std::
    variant<KeyboardShortcut, PointerButtonShortcut, PointerAxisShortcut, FourFingerSwipeShortcut>;

class global_shortcut
{
public:
    global_shortcut(Shortcut&& shortcut, QAction* action);
    ~global_shortcut();

    void invoke() const;
    QAction* action() const;
    Shortcut const& shortcut() const;
    swipe_gesture* swipeGesture() const;

private:
    QSharedPointer<swipe_gesture> m_gesture;
    Shortcut m_shortcut{};
    QAction* m_action{nullptr};
};

}
