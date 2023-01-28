/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

namespace KWin::input::x11
{
class cursor;

class xfixes_cursor_event_filter : public base::x11::event_filter
{
public:
    explicit xfixes_cursor_event_filter(base::x11::event_filter_manager& manager, cursor* cursor);

    bool event(xcb_generic_event_t* event) override;

private:
    cursor* m_cursor;
};

}
