/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef KWIN_XWL_SELECTION
#define KWIN_XWL_SELECTION

#include "selection_utils.h"

#include <QObject>
#include <QVector>

#include <xcb/xcb.h>

#include <memory>

struct xcb_xfixes_selection_notify_event_t;

namespace KWin
{
namespace Xwl
{
class TransferWltoX;
class TransferXtoWl;
template<typename, typename>
class WlSource;
template<typename>
class X11Source;

/*
 * QObject attribute of a Selection.
 * This is a hack around having a template QObject.
 */
class q_selection : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void transferFinished(xcb_timestamp_t eventTime);
};

/**
 * Base class representing generic X selections and their respective
 * Wayland counter-parts.
 *
 * The class needs to be subclassed and adjusted according to the
 * selection, but provides common fucntionality to be expected of all
 * selections.
 *
 * A selection should exist through the whole runtime of an Xwayland
 * session.
 *
 * Independently of each other the class holds the currently active
 * source instance and active transfers relative to the represented
 * selection.
 */
class Selection
{
public:
    bool m_disownPending{false};
    xcb_timestamp_t m_timestamp;
    xcb_window_t m_requestorWindow{XCB_WINDOW_NONE};

    // Active source, if any. Only one of them at max can exist
    // at the same time.
    WlSource<srv_data_device, srv_data_source>* m_waylandSource{nullptr};
    X11Source<clt_data_source>* m_xSource{nullptr};

    // active transfers
    QVector<TransferWltoX*> m_wlToXTransfers;
    QVector<TransferXtoWl*> m_xToWlTransfers;
    QTimer* m_timeoutTransfers = nullptr;

    virtual ~Selection();

    xcb_atom_t atom() const
    {
        return m_atom;
    }
    xcb_window_t window() const
    {
        return m_window;
    }
    xcb_window_t requestorWindow() const
    {
        return m_requestorWindow;
    }

    WlSource<srv_data_device, srv_data_source>* wlSource() const
    {
        return m_waylandSource;
    }

    X11Source<clt_data_source>* x11Source() const
    {
        return m_xSource;
    }

    q_selection* qobject() const
    {
        return m_qobject.get();
    }

    virtual void doHandleXfixesNotify(xcb_xfixes_selection_notify_event_t* event) = 0;

    virtual bool handleClientMessage(xcb_client_message_event_t* event)
    {
        Q_UNUSED(event);
        return false;
    }
    bool handleSelectionRequest(xcb_selection_request_event_t* event);
    bool handleSelectionNotify(xcb_selection_notify_event_t* event);
    bool handlePropertyNotify(xcb_property_notify_event_t* event);

    void startTransferToWayland(xcb_atom_t target, qint32 fd);
    void startTransferToX(xcb_selection_request_event_t* event, qint32 fd);

    virtual void x11OffersChanged(const QStringList& added, const QStringList& removed) = 0;

protected:
    Selection(xcb_atom_t atom);

    void setWindow(xcb_window_t window)
    {
        m_window = window;
    }

    std::unique_ptr<q_selection> m_qobject;

private:
    xcb_atom_t m_atom = XCB_ATOM_NONE;
    xcb_window_t m_window = XCB_WINDOW_NONE;
};

} // namespace Xwl
} // namespace KWin

#endif
