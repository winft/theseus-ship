/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/types.h"
#include "kwinglobals.h"
#include "win/input/global_shortcut.h"

#include <memory>

class QAction;

namespace KWin::input
{

namespace x11
{

class global_shortcuts_manager : public QObject
{
    Q_OBJECT
public:
    global_shortcuts_manager();
    ~global_shortcuts_manager() override;

    std::vector<win::KeyboardShortcut> get_keyboard_shortcut(QKeySequence const& seq);
    QList<QKeySequence> get_keyboard_shortcut(QAction* action);
    QList<QKeySequence> get_keyboard_shortcut(QString const& componentName,
                                              QString const& actionId);

    bool register_keyboard_default_shortcut(QAction* action, QList<QKeySequence> const& shortcut);
    bool register_keyboard_shortcut(QAction* action, QList<QKeySequence> const& shortcut);
    bool override_keyboard_shortcut(QAction* action, QList<QKeySequence> const& shortcut);
    void remove_keyboard_shortcut(QAction* action);

    void registerPointerShortcut(QAction* action,
                                 Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButtons pointerButtons);
    void registerAxisShortcut(QAction* action,
                              Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis);

    void registerTouchpadSwipe(SwipeDirection /*direction*/,
                               uint /*fingerCount*/,
                               QAction* /*action*/,
                               std::function<void(qreal)> /*progressCallback*/)
    {
    }

    void registerTouchpadPinch(PinchDirection /*direction*/,
                               uint /*fingerCount*/,
                               QAction* /*action*/,
                               std::function<void(qreal)> /*progressCallback*/)
    {
    }

    void registerTouchscreenSwipe(QAction* /*action*/,
                                  std::function<void(qreal)> /*progressCallback*/,
                                  SwipeDirection /*direction*/,
                                  uint /*fingerCount*/)
    {
    }

Q_SIGNALS:
    void keyboard_shortcut_changed(QAction* action, QKeySequence const& seq);

private:
    void objectDeleted(QObject* object);
    bool addIfNotExists(win::global_shortcut sc);

    QVector<win::global_shortcut> m_shortcuts;
};

}
}
