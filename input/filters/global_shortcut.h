/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

class QTimer;

namespace KWin::input
{

class KWIN_EXPORT global_shortcut_filter : public event_filter
{
public:
    global_shortcut_filter();
    ~global_shortcut_filter();

    bool button(button_event const& event) override;
    bool axis(axis_event const& event) override;

    bool key(key_event const& event) override;
    bool key_repeat(key_event const& event) override;

    bool swipe_begin(swipe_begin_event const& event) override;
    bool swipe_update(swipe_update_event const& event) override;
    bool swipe_end(swipe_end_event const&) override;

private:
    QTimer* m_powerDown = nullptr;
};

}
