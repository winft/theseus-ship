/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "control.h"

namespace KWin::win
{

control::control(Toplevel* win)
    : m_win{win}
{
}

bool control::skip_pager() const
{
    return m_skip_pager;
}

void control::set_skip_pager(bool set)
{
    m_skip_pager = set;
}

bool control::skip_switcher() const
{
    return m_skip_switcher;
}

void control::set_skip_switcher(bool set)
{
    m_skip_switcher = set;
}

bool control::skip_taskbar() const
{
    return m_skip_taskbar;
}

void control::set_skip_taskbar(bool set)
{
    m_skip_taskbar = set;
}

bool control::original_skip_taskbar() const
{
    return m_original_skip_taskbar;
}

void control::set_original_skip_taskbar(bool set)
{
    m_original_skip_taskbar = set;
}

}
