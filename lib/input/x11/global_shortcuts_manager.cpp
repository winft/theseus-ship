/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcuts_manager.h"

#include "win/input/global_shortcut.h"

#include <KGlobalAccel>
#include <QAction>

namespace KWin::input::x11
{

using global_shortcut = win::global_shortcut;

global_shortcuts_manager::global_shortcuts_manager()
{
    QObject::connect(KGlobalAccel::self(),
                     &KGlobalAccel::globalShortcutChanged,
                     this,
                     &global_shortcuts_manager::keyboard_shortcut_changed);
}

global_shortcuts_manager::~global_shortcuts_manager() = default;

std::vector<win::KeyboardShortcut>
global_shortcuts_manager::get_keyboard_shortcut(QKeySequence const& seq)
{
    return win::get_internal_shortcuts(KGlobalAccel::globalShortcutsByKey(seq));
}

QList<QKeySequence> global_shortcuts_manager::get_keyboard_shortcut(QAction* action)
{
    return KGlobalAccel::self()->shortcut(action);
}

QList<QKeySequence> global_shortcuts_manager::get_keyboard_shortcut(QString const& componentName,
                                                                    QString const& actionId)
{
    return KGlobalAccel::self()->globalShortcut(componentName, actionId);
}

bool global_shortcuts_manager::register_keyboard_default_shortcut(
    QAction* action,
    QList<QKeySequence> const& shortcut)
{
    return KGlobalAccel::self()->setDefaultShortcut(action, shortcut);
}

bool global_shortcuts_manager::register_keyboard_shortcut(QAction* action,
                                                          QList<QKeySequence> const& shortcut)
{
    return KGlobalAccel::self()->setShortcut(action, shortcut, KGlobalAccel::Autoloading);
}

bool global_shortcuts_manager::override_keyboard_shortcut(QAction* action,
                                                          QList<QKeySequence> const& shortcut)
{
    return KGlobalAccel::self()->setShortcut(action, shortcut, KGlobalAccel::NoAutoloading);
}

void global_shortcuts_manager::remove_keyboard_shortcut(QAction* action)
{
    KGlobalAccel::self()->removeAllShortcuts(action);
}

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
    addIfNotExists(global_shortcut(win::PointerButtonShortcut{modifiers, pointerButtons}, action));
}

void global_shortcuts_manager::registerAxisShortcut(QAction* action,
                                                    Qt::KeyboardModifiers modifiers,
                                                    win::pointer_axis_direction axis)
{
    addIfNotExists(global_shortcut(win::PointerAxisShortcut{modifiers, axis}, action));
}

}
