/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "control.h"

#include <config-kwin.h>

#include "abstract_client.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

namespace KWin::win
{

control::control(Toplevel* win)
    : m_win{win}
{
}

void control::setup_tabbox()
{
    assert(!m_tabbox);
#ifdef KWIN_BUILD_TABBOX
    auto abstract_client = dynamic_cast<AbstractClient*>(m_win);
    assert(abstract_client);
    m_tabbox = std::make_shared<TabBox::TabBoxClientImpl>(abstract_client);
#endif
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

std::weak_ptr<TabBox::TabBoxClientImpl> control::tabbox() const
{
    return m_tabbox;
}

bool control::first_in_tabbox() const
{
    return m_first_in_tabbox;
}

void control::set_first_in_tabbox(bool is_first)
{
    m_first_in_tabbox = is_first;
}

}
