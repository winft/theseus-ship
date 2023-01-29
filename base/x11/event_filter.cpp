/*
    SPDX-FileCopyrightText: 2014 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_filter.h"

#include "event_filter_manager.h"

namespace KWin::base::x11
{

event_filter::event_filter(event_filter_manager& manager, QVector<int> const& eventTypes)
    : m_eventTypes(eventTypes)
    , m_extension(0)
    , manager{manager}
{
    manager.register_filter(this);
}

event_filter::event_filter(event_filter_manager& manager,
                           int eventType,
                           int opcode,
                           int genericEventType)
    : event_filter(manager, eventType, opcode, QVector<int>{genericEventType})
{
}

event_filter::event_filter(event_filter_manager& manager,
                           int eventType,
                           int opcode,
                           QVector<int> const& genericEventTypes)
    : m_eventTypes(QVector<int>{eventType})
    , m_extension(opcode)
    , m_genericEventTypes(genericEventTypes)
    , manager{manager}
{
    manager.register_filter(this);
}

event_filter::~event_filter()
{
    manager.unregister_filter(this);
}

bool event_filter::isGenericEvent() const
{
    if (m_eventTypes.count() != 1) {
        return false;
    }
    return m_eventTypes.first() == XCB_GE_GENERIC;
}

}
