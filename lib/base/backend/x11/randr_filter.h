/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include <QTimer>
#include <memory>

class QTimer;

namespace KWin::base::backend::x11
{
class platform;

class RandrFilter : public base::x11::event_filter
{
public:
    explicit RandrFilter(x11::platform* platform);

    bool event(xcb_generic_event_t* event) override;

private:
    x11::platform* platform;
    std::unique_ptr<QTimer> changed_timer;
};

}
