/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_filter_manager.h"

#include "event_filter.h"
#include "event_filter_container.h"

#include "win/space.h"

namespace KWin::base::x11
{

void event_filter_manager::register_filter(event_filter* filter)
{
    if (filter->isGenericEvent()) {
        generic_filters.push_back(new event_filter_container(filter));
    } else {
        filters.push_back(new event_filter_container(filter));
    }
}

static event_filter_container* take_filter(event_filter* filter,
                                           std::vector<QPointer<event_filter_container>>& filters)
{
    auto it = std::find_if(filters.cbegin(), filters.cend(), [filter](auto container) {
        return container->filter() == filter;
    });
    assert(it != filters.cend());

    auto container = *it;
    filters.erase(it);

    return container;
}

void event_filter_manager::unregister_filter(event_filter* filter)
{
    event_filter_container* container = nullptr;

    if (filter->isGenericEvent()) {
        container = take_filter(filter, generic_filters);
    } else {
        container = take_filter(filter, filters);
    }

    delete container;
}

}
