/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "keymap.h"

#include "base/logging.h"

#include <KConfigGroup>
#include <QtGui/private/qxkbcommon_p.h>

namespace KWin::input::xkb
{

keyboard_qobject::~keyboard_qobject() = default;

keyboard::keyboard(xkb_context* context, xkb_compose_table* compose_table)
    : qobject{std::make_unique<keyboard_qobject>()}
    , context{context}
{
    if (compose_table) {
        compose_state = xkb_compose_state_new(compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }
}

keyboard::~keyboard()
{
    xkb_compose_state_unref(compose_state);
    xkb_state_unref(state);
}

void keyboard::install_keymap(int fd, uint32_t size)
{
    try {
        auto map = std::make_shared<xkb::keymap>(fd, size, context);
        foreign_owned = true;
        update_keymap(map);
    } catch (...) {
        // Do nothing for now.
        qCWarning(KWIN_CORE) << "Keymap could not be installed from fd" << fd;
    }
}

void keyboard::update(std::shared_ptr<xkb::keymap> keymap, std::vector<std::string> const& layouts)
{
    if (foreign_owned) {
        return;
    }
    this->layouts = layouts;
    update_keymap(keymap);
}

void keyboard::update_keymap(std::shared_ptr<xkb::keymap> keymap)
{
    auto state = xkb_state_new(keymap->raw);
    if (!state) {
        qCDebug(KWIN_CORE) << "Could not create XKB state";
        return;
    }

    // now release the old ones
    xkb_state_unref(this->state);

    this->keymap = keymap;
    this->state = state;

    modifiers_indices.shift = xkb_keymap_mod_get_index(keymap->raw, XKB_MOD_NAME_SHIFT);
    modifiers_indices.caps = xkb_keymap_mod_get_index(keymap->raw, XKB_MOD_NAME_CAPS);
    modifiers_indices.ctrl = xkb_keymap_mod_get_index(keymap->raw, XKB_MOD_NAME_CTRL);
    modifiers_indices.alt = xkb_keymap_mod_get_index(keymap->raw, XKB_MOD_NAME_ALT);
    modifiers_indices.meta = xkb_keymap_mod_get_index(keymap->raw, XKB_MOD_NAME_LOGO);
    modifiers_indices.num = xkb_keymap_mod_get_index(keymap->raw, XKB_MOD_NAME_NUM);

    leds_indices.num = xkb_keymap_led_get_index(keymap->raw, XKB_LED_NAME_NUM);
    leds_indices.caps = xkb_keymap_led_get_index(keymap->raw, XKB_LED_NAME_CAPS);
    leds_indices.scroll = xkb_keymap_led_get_index(keymap->raw, XKB_LED_NAME_SCROLL);

    layout = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

    modifier_state.depressed
        = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_DEPRESSED));
    modifier_state.latched
        = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_LATCHED));
    modifier_state.locked
        = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_LOCKED));

    update_modifiers();
}

void keyboard::update_modifiers(uint32_t modsDepressed,
                                uint32_t modsLatched,
                                uint32_t modsLocked,
                                uint32_t group)
{
    if (!keymap || !state) {
        return;
    }
    xkb_state_update_mask(state, modsDepressed, modsLatched, modsLocked, 0, 0, group);
    update_modifiers();
    forward_modifiers();
}

void keyboard::update_key(uint32_t key, key_state state)
{
    if (!keymap || !this->state) {
        return;
    }

    forward_modifiers();

    xkb_state_update_key(this->state, key + 8, static_cast<xkb_key_direction>(state));
    if (state == key_state::pressed) {
        const auto sym = to_keysym(key);
        if (compose_state
            && xkb_compose_state_feed(compose_state, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(compose_state)) {
            case XKB_COMPOSE_NOTHING:
                keysym = sym;
                break;
            case XKB_COMPOSE_COMPOSED:
                keysym = xkb_compose_state_get_one_sym(compose_state);
                break;
            default:
                keysym = XKB_KEY_NoSymbol;
                break;
            }
        } else {
            keysym = sym;
        }
    }

    update_modifiers();
    update_consumed_modifiers(key);
}

void keyboard::update_modifiers()
{
    constexpr auto is_active = xkb_state_mod_index_is_active;
    auto mods = Qt::KeyboardModifiers();

    if (is_active(state, modifiers_indices.shift, XKB_STATE_MODS_EFFECTIVE) == 1
        || is_active(state, modifiers_indices.caps, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ShiftModifier;
    }
    if (is_active(state, modifiers_indices.alt, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::AltModifier;
    }
    if (is_active(state, modifiers_indices.ctrl, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ControlModifier;
    }
    if (is_active(state, modifiers_indices.meta, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::MetaModifier;
    }
    if (keysym >= XKB_KEY_KP_Space && keysym <= XKB_KEY_KP_9) {
        mods |= Qt::KeypadModifier;
    }

    qt_modifiers = mods;

    // update LEDs
    auto leds{keyboard_leds::none};
    if (xkb_state_led_index_is_active(state, leds_indices.num) == 1) {
        leds = leds | keyboard_leds::num_lock;
    }
    if (xkb_state_led_index_is_active(state, leds_indices.caps) == 1) {
        leds = leds | keyboard_leds::caps_lock;
    }
    if (xkb_state_led_index_is_active(state, leds_indices.scroll) == 1) {
        leds = leds | keyboard_leds::scroll_lock;
    }
    if (this->leds != leds) {
        this->leds = leds;
        Q_EMIT qobject->leds_changed(leds);
    }

    modifier_state.depressed
        = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_DEPRESSED));
    modifier_state.latched
        = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_LATCHED));
    modifier_state.locked
        = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_LOCKED));

    auto old_layout = layout;
    layout = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

    if (old_layout != layout) {
        Q_EMIT qobject->layout_changed();
    }
}

void keyboard::forward_modifiers()
{
    if (forward_modifiers_impl) {
        forward_modifiers_impl(keymap.get(), modifier_state, layout);
    }
}

std::string keyboard::layout_name_from_index(xkb_layout_index_t index) const
{
    if (!keymap) {
        return {};
    }
    return std::string(xkb_keymap_layout_get_name(keymap->raw, index));
}

std::string keyboard::layout_name() const
{
    return layout_name_from_index(layout);
}

std::string const& keyboard::layout_short_name_from_index(int index) const
{
    return layouts.at(index);
}

void keyboard::update_consumed_modifiers(uint32_t key)
{
    constexpr auto is_consumed = xkb_state_mod_index_is_consumed2;
    auto mods = Qt::KeyboardModifiers();

    key += 8;

    if (is_consumed(state, key, modifiers_indices.shift, XKB_CONSUMED_MODE_GTK) == 1) {
        mods |= Qt::ShiftModifier;
    }
    if (is_consumed(state, key, modifiers_indices.alt, XKB_CONSUMED_MODE_GTK) == 1) {
        mods |= Qt::AltModifier;
    }
    if (is_consumed(state, key, modifiers_indices.ctrl, XKB_CONSUMED_MODE_GTK) == 1) {
        mods |= Qt::ControlModifier;
    }
    if (is_consumed(state, key, modifiers_indices.meta, XKB_CONSUMED_MODE_GTK) == 1) {
        mods |= Qt::MetaModifier;
    }

    qt_modifiers_consumed = mods;
}

Qt::KeyboardModifiers keyboard::modifiers_relevant_for_global_shortcuts(uint32_t scanCode) const
{
    if (!state) {
        return {};
    }

    constexpr auto is_active = xkb_state_mod_index_is_active;
    auto mods = Qt::KeyboardModifiers();

    if (is_active(state, modifiers_indices.shift, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ShiftModifier;
    }
    if (is_active(state, modifiers_indices.alt, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::AltModifier;
    }
    if (is_active(state, modifiers_indices.ctrl, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ControlModifier;
    }
    if (is_active(state, modifiers_indices.meta, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::MetaModifier;
    }

    auto consumed_mods = qt_modifiers_consumed;
    if ((mods & Qt::ShiftModifier) && (consumed_mods == Qt::ShiftModifier)) {
        // test whether current keysym is a letter
        // in that case the shift should be removed from the consumed modifiers again
        // otherwise it would not be possible to trigger e.g. Shift+W as a shortcut
        // see BUG: 370341
        if (QChar::isLetter(to_qt_key(keysym, scanCode, Qt::ControlModifier))) {
            consumed_mods = Qt::KeyboardModifiers();
        }
    }

    return mods & ~consumed_mods;
}

xkb_keysym_t keyboard::to_keysym(uint32_t key)
{
    if (!state) {
        return XKB_KEY_NoSymbol;
    }
    return xkb_state_key_get_one_sym(state, key + 8);
}

std::string keyboard::to_string(xkb_keysym_t keysym)
{
    if (!state || keysym == XKB_KEY_NoSymbol) {
        return {};
    }

    QByteArray byteArray(7, 0);
    auto ok = xkb_keysym_to_utf8(keysym, byteArray.data(), byteArray.size());
    if (ok == -1 || ok == 0) {
        return {};
    }

    return std::string(byteArray.constData());
}

Qt::Key keyboard::to_qt_key(xkb_keysym_t keysym,
                            uint32_t scanCode,
                            Qt::KeyboardModifiers modifiers,
                            bool superAsMeta) const
{
    return Qt::Key(QXkbCommon::keysymToQtKey(keysym, modifiers, state, scanCode + 8, superAsMeta));
}

bool keyboard::should_key_repeat(uint32_t key) const
{
    if (!keymap) {
        return false;
    }
    return xkb_keymap_key_repeats(keymap->raw, key + 8) != 0;
}

void keyboard::switch_to_next_layout()
{
    if (!keymap || !state) {
        return;
    }

    auto num_layouts = xkb_keymap_num_layouts(keymap->raw);
    auto next = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE) + 1;

    switch_to_layout(next % num_layouts);
}

void keyboard::switch_to_previous_layout()
{
    if (!keymap || !state) {
        return;
    }
    const xkb_layout_index_t previousLayout = layout == 0 ? layouts_count() - 1 : layout - 1;
    switch_to_layout(previousLayout);
}

bool keyboard::switch_to_layout(xkb_layout_index_t layout)
{
    if (!keymap || !state || layout >= layouts_count()) {
        return false;
    }

    auto depressed = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_DEPRESSED));
    auto latched = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_LATCHED));
    auto locked = xkb_state_serialize_mods(state, xkb_state_component(XKB_STATE_MODS_LOCKED));

    xkb_state_update_mask(state, depressed, latched, locked, 0, 0, layout);
    update_modifiers();
    forward_modifiers();
    return true;
}

uint32_t keyboard::layouts_count() const
{
    if (!keymap) {
        return 0;
    }
    return xkb_keymap_num_layouts(keymap->raw);
}

}
