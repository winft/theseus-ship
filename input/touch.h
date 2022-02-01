/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/touch.h"
#include "event.h"

#include "kwin_export.h"

#include <QObject>
#include <QPointF>

namespace KWin
{
namespace base::wayland
{
class output;
}

namespace input
{

class platform;

class KWIN_EXPORT touch : public QObject
{
    Q_OBJECT
public:
    input::platform* platform;
    std::unique_ptr<control::touch> control;
    base::wayland::output* output{nullptr};

    touch(input::platform* platform);
    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;
    ~touch() override;

    // TODO(romangg): Make this a function template.
    base::wayland::output* get_output() const;

Q_SIGNALS:
    void down(touch_down_event);
    void up(touch_up_event);
    void motion(touch_motion_event);
    void cancel(touch_cancel_event);
    void frame();
};

}
}
