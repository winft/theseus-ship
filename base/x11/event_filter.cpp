/*
    SPDX-FileCopyrightText: 2014 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "event_filter.h"

#include "event_filter_manager.h"
#include "main.h"

namespace KWin::platform::x11
{

event_filter::event_filter(const QVector<int>& eventTypes)
    : m_eventTypes(eventTypes)
    , m_extension(0)
{
    kwinApp()->x11_event_filters->register_filter(this);
}

event_filter::event_filter(int eventType, int opcode, int genericEventType)
    : event_filter(eventType, opcode, QVector<int>{genericEventType})
{
}

event_filter::event_filter(int eventType, int opcode, const QVector<int>& genericEventTypes)
    : m_eventTypes(QVector<int>{eventType})
    , m_extension(opcode)
    , m_genericEventTypes(genericEventTypes)
{
    kwinApp()->x11_event_filters->register_filter(this);
}

event_filter::~event_filter()
{
    kwinApp()->x11_event_filters->unregister_filter(this);
}

bool event_filter::isGenericEvent() const
{
    if (m_eventTypes.count() != 1) {
        return false;
    }
    return m_eventTypes.first() == XCB_GE_GENERIC;
}

}
