/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/pointer.h"

#include <kwin_export.h>

#include <QObject>
#include <QPointF>

namespace KWin::input
{

class platform;
class pointer;

template<typename Device>
struct event {
    Device* dev{nullptr};
    uint32_t time_msec;
};

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

class KWIN_EXPORT pointer : public QObject
{
    Q_OBJECT
public:
    input::platform* plat;
    control::pointer* control{nullptr};

    pointer(platform* plat, QObject* parent = nullptr);
    pointer(pointer const&) = delete;
    pointer& operator=(pointer const&) = delete;
    pointer(pointer&& other) noexcept = default;
    pointer& operator=(pointer&& other) noexcept = default;
    ~pointer();

Q_SIGNALS:
    void motion(motion_event);
    void motion_absolute(motion_absolute_event);
    void button_changed(button_event);
    void axis_changed(axis_event);
    void frame();
    void swipe_begin(swipe_begin_event);
    void swipe_update(swipe_update_event);
    void swipe_end(swipe_end_event);
    void pinch_begin(pinch_begin_event);
    void pinch_update(pinch_update_event);
    void pinch_end(pinch_end_event);
};

}
