/*
    SPDX-FileCopyrightText: 2018 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_spy.h"

namespace KWin::input
{

template<typename Redirect>
class touch_hide_cursor_spy : public event_spy<Redirect>
{
public:
    explicit touch_hide_cursor_spy(Redirect& redirect)
        : event_spy<Redirect>(redirect)
    {
    }

    void button(button_event const& /*event*/) override
    {
        showCursor();
    }

    void motion(motion_event const& /*event*/) override
    {
        showCursor();
    }

    void axis(axis_event const& /*event*/) override
    {
        showCursor();
    }

    void touch_down(touch_down_event const& /*event*/) override
    {
        hideCursor();
    }

private:
    void showCursor()
    {
        if (!m_cursorHidden) {
            return;
        }
        m_cursorHidden = false;
        this->redirect.cursor->show();
    }

    void hideCursor()
    {
        if (m_cursorHidden) {
            return;
        }
        m_cursorHidden = true;
        this->redirect.cursor->hide();
    }

    bool m_cursorHidden = false;
};

}
