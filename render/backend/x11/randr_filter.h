/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

class QTimer;

namespace KWin::render::backend::x11
{
class X11StandalonePlatform;

class RandrFilter : public base::x11::event_filter
{
public:
    explicit RandrFilter(X11StandalonePlatform* backend);

    bool event(xcb_generic_event_t* event) override;

private:
    X11StandalonePlatform* m_backend;
    QTimer* m_changedTimer;
};

}
