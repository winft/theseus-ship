/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/pointer.h"
#include "event.h"

#include <kwin_export.h>

#include <QObject>

namespace KWin::input
{

class platform;

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
    ~pointer() override = default;

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
