/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QMetaType>
#include <QPointF>
#include <cstdint>

namespace KWin::input
{
class keyboard;
class pointer;
class switch_device;
class touch;

template<typename Device>
struct event {
    Device* dev{nullptr};
    uint32_t time_msec;
};

/** Pointer events */

enum class axis_orientation {
    vertical,
    horizontal,
};

enum class axis_source {
    unknown,
    wheel,
    finger,
    continuous,
    wheel_tilt,
};

enum class button_state {
    released,
    pressed,
};

struct button_event {
    uint32_t key;
    button_state state;
    event<pointer> base;
};

struct motion_event {
    QPointF delta;
    QPointF unaccel_delta;
    event<pointer> base;
};

struct motion_absolute_event {
    QPointF pos;
    event<pointer> base;
};

struct axis_event {
    axis_source source;
    axis_orientation orientation;
    double delta;
    int32_t delta_discrete;
    event<pointer> base;
};

struct swipe_begin_event {
    uint32_t fingers;
    event<pointer> base;
};

struct swipe_update_event {
    uint32_t fingers;
    QPointF delta;
    event<pointer> base;
};

struct swipe_end_event {
    bool cancelled{false};
    event<pointer> base;
};

struct pinch_begin_event {
    uint32_t fingers;
    event<pointer> base;
};

struct pinch_update_event {
    uint32_t fingers;
    QPointF delta;
    double scale;
    double rotation;
    event<pointer> base;
};

struct pinch_end_event {
    bool cancelled{false};
    event<pointer> base;
};

/** Keyboard events */

enum class key_state {
    released,
    pressed,
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
    key_state state;
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

/** Touch events */

struct touch_down_event {
    int32_t id;
    QPointF pos;
    event<touch> base;
};

struct touch_up_event {
    int32_t id;
    event<touch> base;
};

struct touch_motion_event {
    int32_t id;
    QPointF pos;
    event<touch> base;
};

struct touch_cancel_event {
    int32_t id;
    event<touch> base;
};

/** Switch events */

enum class switch_type {
    lid = 1,
    tablet_mode,
};

enum class switch_state {
    off = 0,
    on,
    toggle,
};

struct switch_toggle_event {
    switch_type type;
    switch_state state;
    event<switch_device> base;
};

}

Q_DECLARE_METATYPE(KWin::input::button_state)
Q_DECLARE_METATYPE(KWin::input::key_event)
Q_DECLARE_METATYPE(KWin::input::key_state)
