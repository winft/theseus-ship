/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "pointer.h"

#include <kwin_export.h>

#include <QObject>

namespace KWin::input
{

class keyboard;

enum class keyboard_led {
    num_lock,
    caps_lock,
    scroll_lock,
};

enum class modifier {
    shift,
    caps,
    ctrl,
    alt,
    mod2,
    mod3,
    logo,
    mod5,
};

struct key_event {
    uint32_t keycode;
    button_state state;
    bool requires_modifier_update;
    event<keyboard> base;
};

struct modifiers_event {
    uint32_t depressed;
    uint32_t latched;
    uint32_t locked;
    uint32_t group;
    struct {
        keyboard* dev;
    } base;
};

class KWIN_EXPORT keyboard : public QObject
{
    Q_OBJECT
public:
    input::platform* plat;

    keyboard(platform* plat, QObject* parent = nullptr);
    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    keyboard(keyboard&& other) noexcept = default;
    keyboard& operator=(keyboard&& other) noexcept = default;
    ~keyboard();

Q_SIGNALS:
    void key_changed(key_event);
    void modifiers_changed(modifiers_event);
};

}
