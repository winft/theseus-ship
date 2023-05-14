/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "win/types.h"

#include <QKeySequence>
#include <QSharedPointer>
#include <string>

class QAction;

namespace KWin::win
{

class swipe_gesture;
class pinch_gesture;

struct KeyboardShortcut {
    bool operator==(const KeyboardShortcut& rhs) const
    {
        return sequence == rhs.sequence;
    }

    QKeySequence sequence;
    std::string id;
    std::string name;
    std::string consumer;
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
    pointer_axis_direction axisDirection;
    bool operator==(const PointerAxisShortcut& rhs) const
    {
        return axisModifiers == rhs.axisModifiers && axisDirection == rhs.axisDirection;
    }
};

struct RealtimeFeedbackSwipeShortcut {
    input_device_type device;
    swipe_direction direction;
    std::function<void(qreal)> progressCallback;
    uint fingerCount;

    template<typename T>
    bool operator==(const T& rhs) const
    {
        return direction == rhs.direction && fingerCount == rhs.fingerCount && device == rhs.device;
    }
};

struct RealtimeFeedbackPinchShortcut {
    pinch_direction direction;
    std::function<void(qreal)> scaleCallback;
    uint fingerCount;

    template<typename T>
    bool operator==(const T& rhs) const
    {
        return direction == rhs.direction && fingerCount == rhs.fingerCount;
    }
};

using Shortcut = std::variant<KeyboardShortcut,
                              PointerButtonShortcut,
                              PointerAxisShortcut,
                              RealtimeFeedbackSwipeShortcut,
                              RealtimeFeedbackPinchShortcut>;

class KWIN_EXPORT global_shortcut
{
public:
    global_shortcut(Shortcut&& shortcut, QAction* action);
    ~global_shortcut();

    void invoke() const;
    QAction* action() const;
    Shortcut const& shortcut() const;
    swipe_gesture* swipeGesture() const;
    pinch_gesture* pinchGesture() const;

private:
    QSharedPointer<swipe_gesture> m_swipeGesture;
    QSharedPointer<pinch_gesture> m_pinchGesture;
    Shortcut m_shortcut{};
    QAction* m_action{nullptr};
};

template<typename ShortcutInfo>
std::vector<KeyboardShortcut> get_internal_shortcuts(QList<ShortcutInfo> const& list)
{
    std::vector<KeyboardShortcut> ret;
    ret.reserve(list.size());

    for (auto&& el : qAsConst(list)) {
        auto const keys = el.keys();
        auto const seq = keys.empty() ? QKeySequence() : keys.front();
        ret.push_back(KeyboardShortcut{.sequence = seq,
                                       .id = el.uniqueName().toStdString(),
                                       .name = el.friendlyName().toStdString(),
                                       .consumer = el.componentFriendlyName().toStdString()});
    }
    return ret;
}

}
