/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/touch.h"
#include "event.h"

#include <config-kwin.h>
#include <kwin_export.h>

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
    input::platform* plat;
    control::touch* control{nullptr};
    base::wayland::output* output{nullptr};

    touch(platform* plat, QObject* parent = nullptr);
    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;
    touch(touch&& other) noexcept = default;
    touch& operator=(touch&& other) noexcept = default;
    ~touch();

    // TODO(romangg): Make this a function template.
    base::wayland::output* get_output() const;

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
