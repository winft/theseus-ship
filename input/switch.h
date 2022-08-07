/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/switch.h"
#include "event.h"

#include "kwin_export.h"

#include <QObject>

namespace KWin::input
{

class platform;

class KWIN_EXPORT switch_device : public QObject
{
    Q_OBJECT
public:
    input::platform* platform;
    std::unique_ptr<control::switch_device> control;

    switch_device(input::platform* platform);
    switch_device(switch_device const&) = delete;
    switch_device& operator=(switch_device const&) = delete;

Q_SIGNALS:
    void toggle(switch_toggle_event);
};

}
