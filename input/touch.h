/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/touch.h"
#include "pointer.h"

#include <config-kwin.h>
#include <kwin_export.h>

#include <QObject>
#include <QPointF>

namespace KWin
{
class AbstractWaylandOutput;

namespace input
{

class platform;
class touch;

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

class KWIN_EXPORT touch : public QObject
{
    Q_OBJECT
public:
    input::platform* plat;
    control::touch* control{nullptr};
    AbstractWaylandOutput* output{nullptr};

    touch(platform* plat, QObject* parent = nullptr);
    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;
    touch(touch&& other) noexcept = default;
    touch& operator=(touch&& other) noexcept = default;
    ~touch();

    // TODO(romangg): Make this a function template.
    AbstractWaylandOutput* get_output() const;

Q_SIGNALS:
    void down(touch_down_event);
    void up(touch_up_event);
    void motion(touch_motion_event);
    void cancel(touch_cancel_event);
#if HAVE_WLR_TOUCH_FRAME
    void frame();
#endif
};

}
}
