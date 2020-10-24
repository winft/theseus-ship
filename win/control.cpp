/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "control.h"
#include "stacking.h"

#include <config-kwin.h>

#include "abstract_client.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

#include <QObject>
#include <QTimer>

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

QIcon const& control::icon() const
{
    return m_icon;
}

void control::set_icon(QIcon const& icon)
{
    m_icon = icon;
    Q_EMIT m_win->iconChanged();
}

bool control::active() const
{
    return m_active;
}

void control::set_active(bool active)
{
    m_active = active;
}

bool control::keep_above() const
{
    return m_keep_above;
}

void control::set_keep_above(bool keep)
{
    m_keep_above = keep;
}

bool control::keep_below() const
{
    return m_keep_below;
}
void control::set_keep_below(bool keep)
{
    m_keep_below = keep;
}

void control::set_demands_attention(bool set)
{
    m_demands_attention = set;
}

bool control::demands_attention() const
{
    return m_demands_attention;
}

void control::start_auto_raise()
{
    delete m_auto_raise_timer;
    m_auto_raise_timer = new QTimer(m_win);
    QObject::connect(m_auto_raise_timer, &QTimer::timeout, m_win, [this] { auto_raise(m_win); });
    m_auto_raise_timer->setSingleShot(true);
    m_auto_raise_timer->start(options->autoRaiseInterval());
}

void control::cancel_auto_raise()
{
    delete m_auto_raise_timer;
    m_auto_raise_timer = nullptr;
}

void control::update_mouse_grab()
{
}

}
