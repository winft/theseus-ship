/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <memory>

class QAction;

namespace KWin::input
{

class global_shortcut;

namespace x11
{

class global_shortcuts_manager : public QObject
{
public:
    global_shortcuts_manager();
    ~global_shortcuts_manager() override;

    void registerPointerShortcut(QAction* action,
                                 Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButtons pointerButtons);
    void registerAxisShortcut(QAction* action,
                              Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis);

    void registerTouchpadSwipe(QAction* /*action*/,
                               SwipeDirection /*direction*/,
                               uint /*fingerCount*/ = 4)
    {
    }

    void registerRealtimeTouchpadSwipe(QAction* /*onUp*/,
                                       std::function<void(qreal)> /*progressCallback*/,
                                       SwipeDirection /*direction*/,
                                       uint /*fingerCount*/ = 4)
    {
    }

    void registerTouchpadPinch(QAction* /*action*/,
                               PinchDirection /*direction*/,
                               uint /*fingerCount*/ = 4)
    {
    }

    void registerRealtimeTouchpadPinch(QAction* /*onUp*/,
                                       std::function<void(qreal)> /*progressCallback*/,
                                       PinchDirection /*direction*/,
                                       uint /*fingerCount*/ = 4)
    {
    }

    void registerTouchscreenSwipe(QAction* /*action*/,
                                  std::function<void(qreal)> /*progressCallback*/,
                                  SwipeDirection /*direction*/,
                                  uint /*fingerCount*/)
    {
    }

private:
    void objectDeleted(QObject* object);
    bool addIfNotExists(global_shortcut sc);

    QVector<global_shortcut> m_shortcuts;
};

}
}
