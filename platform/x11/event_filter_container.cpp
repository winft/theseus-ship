/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_filter_container.h"

namespace KWin::platform::x11
{

event_filter_container::event_filter_container(X11EventFilter* filter)
    : m_filter(filter)
{
}

X11EventFilter* event_filter_container::filter() const
{
    return m_filter;
}

}
