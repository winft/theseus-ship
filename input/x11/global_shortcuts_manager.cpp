/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcuts_manager.h"

#include "input/global_shortcut.h"

#include <QAction>

namespace KWin::input::x11
{

global_shortcuts_manager::global_shortcuts_manager()
{
}

global_shortcuts_manager::~global_shortcuts_manager() = default;

void global_shortcuts_manager::objectDeleted(QObject* object)
{
    auto it = m_shortcuts.begin();
    while (it != m_shortcuts.end()) {
        if (it->action() == object) {
            it = m_shortcuts.erase(it);
        } else {
            ++it;
        }
    }
}

bool global_shortcuts_manager::addIfNotExists(global_shortcut sc)
{
    for (const auto& cs : qAsConst(m_shortcuts)) {
        if (sc.shortcut() == cs.shortcut()) {
            return false;
        }
    }

    QObject::connect(
        sc.action(), &QAction::destroyed, this, &global_shortcuts_manager::objectDeleted);
    m_shortcuts.push_back(std::move(sc));
    return true;
}

void global_shortcuts_manager::registerPointerShortcut(QAction* action,
                                                       Qt::KeyboardModifiers modifiers,
                                                       Qt::MouseButtons pointerButtons)
{
    addIfNotExists(global_shortcut(PointerButtonShortcut{modifiers, pointerButtons}, action));
}

void global_shortcuts_manager::registerAxisShortcut(QAction* action,
                                                    Qt::KeyboardModifiers modifiers,
                                                    PointerAxisDirection axis)
{
    addIfNotExists(global_shortcut(PointerAxisShortcut{modifiers, axis}, action));
}

}
