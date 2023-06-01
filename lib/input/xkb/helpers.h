/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <Qt>

namespace KWin::input::xkb
{

/**
 * Retuns the first configurable keyboard, otherwise the default-created keyboard is returned.
 */
template<typename Platform>
auto get_primary_xkb_keyboard(Platform const& platform)
{
    auto const& keyboards = platform.keyboards;

    for (auto& keyboard : keyboards) {
        if (keyboard->xkb->foreign_owned) {
            // A foreign owned keyboard never is the primary keyboard.
            continue;
        }
        if (auto& ctrl = keyboard->control; ctrl && !ctrl->is_alpha_numeric_keyboard()) {
            // Filter out keyboard-like devices, for example power buttons under libinput.
            continue;
        }
        return keyboard->xkb.get();
    }

    return platform.xkb.default_keyboard.get();
}

template<typename Platform>
Qt::KeyboardModifiers get_active_keyboard_modifiers(Platform const& platform)
{
    Qt::KeyboardModifiers all{Qt::NoModifier};

    for (auto keyboard : platform.keyboards) {
        all |= keyboard->xkb->qt_modifiers;
    }

    return all;
}

template<typename Platform>
Qt::KeyboardModifiers
get_active_keyboard_modifiers_relevant_for_global_shortcuts(Platform const& platform)
{
    Qt::KeyboardModifiers all{Qt::NoModifier};

    for (auto keyboard : platform.keyboards) {
        all |= keyboard->xkb->modifiers_relevant_for_global_shortcuts();
    }

    return all;
}

template<typename Manager, typename Keyboard>
void keyboard_update_from_default(Manager const& manager, Keyboard& keys)
{
    if (auto const& dkeys = manager.default_keyboard; dkeys->keymap) {
        keys.update(dkeys->keymap, dkeys->layouts);
    }
}

}
