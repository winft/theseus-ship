/*
    SPDX-FileCopyrightText: 2014 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QVector>
#include <xcb/xcb.h>

namespace KWin::base::x11
{

class KWIN_EXPORT event_filter
{
public:
    /**
     * Creates an event filter for the given event type.
     */
    event_filter(int eventType, int opcode = 0, int genericEventType = 0);
    event_filter(int eventType, int opcode, const QVector<int>& genericEventTypes);
    event_filter(const QVector<int>& eventTypes);

    /**
     * Destroys the event filter.
     */
    virtual ~event_filter();

    /**
     * Returns the type of events to filter.
     */
    QVector<int> eventTypes() const
    {
        return m_eventTypes;
    }

    /**
     * Returns the major opcode of the extension.
     *
     * Only used when the event type is XCB_GE_GENERIC.
     */
    int extension() const
    {
        return m_extension;
    }

    /**
     * Returns the types of generic events to filter.
     *
     * Only used when the event type is XCB_GE_GENERIC.
     */
    QVector<int> genericEventTypes() const
    {
        return m_genericEventTypes;
    }

    /**
     * This method is called for every event of the filtered type.
     *
     * Return true to accept the event and stop further processing, and false otherwise.
     */
    virtual bool event(xcb_generic_event_t* event) = 0;

    /**
     * Whether the event filter is for XCB_GE_GENERIC events.
     */
    bool isGenericEvent() const;

private:
    QVector<int> m_eventTypes;
    int m_extension;
    QVector<int> m_genericEventTypes;
};

}
