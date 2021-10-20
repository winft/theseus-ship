/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QPointer>

namespace KWin::platform::x11
{
class event_filter;
class event_filter_container;

class KWIN_EXPORT event_filter_manager
{
public:
    std::vector<QPointer<platform::x11::event_filter_container>> filters;
    std::vector<QPointer<platform::x11::event_filter_container>> generic_filters;

    void register_filter(event_filter* filter);
    void unregister_filter(event_filter* filter);
};

}
