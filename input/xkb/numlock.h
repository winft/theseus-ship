/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KConfigGroup>
#include <KSharedConfig>

#include <bitset>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace KWin::input::xkb
{

enum class latched_key_change {
    off,
    on,
    unchanged,
};

template<typename Manager>
latched_key_change read_startup_num_lock_config(Manager const& manager)
{
    if (!manager.numlock_config) {
        return latched_key_change::unchanged;
    }

    // STATE_ON = 0,  STATE_OFF = 1, STATE_UNCHANGED = 2, see plasma-desktop/kcms/keyboard/kcmmisc.h
    auto const config = manager.numlock_config->group("Keyboard");
    auto setting = config.readEntry("NumLock", 2);

    if (setting == 0) {
        return latched_key_change::on;
    }
    if (setting == 1) {
        return latched_key_change::off;
    }

    return latched_key_change::unchanged;
}

template<typename Manager, typename Keyboard>
void numlock_evaluate_startup(Manager const& manager, Keyboard& keys)
{
    if (keys.startup_num_lock_done) {
        return;
    }
    keys.startup_num_lock_done = true;

    if (keys.foreign_owned || keys.modifiers_indices.num == XKB_MOD_INVALID) {
        return;
    }

    auto const setting = read_startup_num_lock_config(manager);
    if (setting == latched_key_change::unchanged) {
        // We keep the current state.
        return;
    }

    auto num_lock_is_active = xkb_state_mod_index_is_active(
        keys.state, keys.modifiers_indices.num, XKB_STATE_MODS_LOCKED);
    if (num_lock_is_active < 0) {
        // Index not available
        return;
    }

    auto num_lock_current = num_lock_is_active ? latched_key_change::on : latched_key_change::off;

    if (setting == num_lock_current) {
        // Nothing to change.
        return;
    }

    auto mask = std::bitset<sizeof(xkb_mod_mask_t) * 8>{keys.modifier_state.locked};

    if (mask.size() <= keys.modifiers_indices.num) {
        // Not enough space in the mask for the num lock.
        return;
    }

    mask[keys.modifiers_indices.num] = (setting == latched_key_change::on);
    keys.modifier_state.locked = mask.to_ulong();

    xkb_state_update_mask(keys.state,
                          keys.modifier_state.depressed,
                          keys.modifier_state.latched,
                          keys.modifier_state.locked,
                          0,
                          0,
                          keys.layout);

    keys.modifier_state.locked
        = xkb_state_serialize_mods(keys.state, xkb_state_component(XKB_STATE_MODS_LOCKED));

    keys.update_modifiers();
}

}
