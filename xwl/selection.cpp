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
#include "selection.h"
#include "databridge.h"

#include "atoms.h"

namespace KWin
{
namespace Xwl
{

Selection::Selection(xcb_atom_t atom)
    : m_qobject(new q_selection)
    , m_atom(atom)
{
    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    m_window = xcb_generate_id(kwinApp()->x11Connection());
    m_requestorWindow = m_window;
    xcb_flush(xcbConn);
}

Selection::~Selection()
{
    delete m_waylandSource;
    delete m_xSource;
    m_waylandSource = nullptr;
    m_xSource = nullptr;
}

void Selection::startTransferToWayland(xcb_atom_t target, qint32 fd)
{
    // create new x to wl data transfer object
    auto* transfer = new TransferXtoWl(
        m_atom, target, fd, m_xSource->timestamp(), m_requestorWindow, qobject());
    m_xToWlTransfers << transfer;

    QObject::connect(transfer, &TransferXtoWl::finished, qobject(), [this, transfer]() {
        Q_EMIT qobject()->transferFinished(transfer->timestamp());
        delete transfer;
        m_xToWlTransfers.removeOne(transfer);
        end_timeout_transfers_timer(this);
    });
    start_timeout_transfers_timer(this);
}

void Selection::startTransferToX(xcb_selection_request_event_t* event, qint32 fd)
{
    // create new wl to x data transfer object
    auto* transfer = new TransferWltoX(m_atom, event, fd, qobject());

    QObject::connect(transfer, &TransferWltoX::selectionNotify, qobject(), &sendSelectionNotify);
    QObject::connect(transfer, &TransferWltoX::finished, qobject(), [this, transfer]() {
        Q_EMIT qobject()->transferFinished(transfer->timestamp());

        // TODO: serialize? see comment below.
        //        const bool wasActive = (transfer == m_wlToXTransfers[0]);
        delete transfer;
        m_wlToXTransfers.removeOne(transfer);
        end_timeout_transfers_timer(this);
        //        if (wasActive && !m_wlToXTransfers.isEmpty()) {
        //            m_wlToXTransfers[0]->startTransferFromSource();
        //        }
    });

    // add it to list of queued transfers
    m_wlToXTransfers.append(transfer);

    // TODO: Do we need to serialize the transfers, or can we do
    //       them in parallel as we do it right now?
    transfer->startTransferFromSource();
    //    if (m_wlToXTransfers.size() == 1) {
    //        transfer->startTransferFromSource();
    //    }
    start_timeout_transfers_timer(this);
}

} // namespace Xwl
} // namespace KWin
