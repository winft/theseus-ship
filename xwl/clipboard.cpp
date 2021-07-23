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
#include "clipboard.h"

#include "selection_source.h"
#include "transfer.h"
#include "xwayland.h"

#include "wayland_server.h"
#include "workspace.h"

#include "win/x11/window.h"

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/datadevice.h>
#include <Wrapland/Client/datasource.h>

#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/seat.h>

#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>

#include <xwayland_logging.h>

namespace KWin
{
namespace Xwl
{

Clipboard::Clipboard(xcb_atom_t atom, srv_data_device* srv_dev, clt_data_device* clt_dev)
{
    data = create_selection_data(atom, srv_dev, clt_dev);

    xcb_connection_t* xcbConn = kwinApp()->x11Connection();

    const uint32_t clipboardValues[]
        = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(xcbConn,
                      XCB_COPY_FROM_PARENT,
                      data.window,
                      kwinApp()->x11RootWindow(),
                      0,
                      0,
                      10,
                      10,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      Xwayland::self()->xcbScreen()->root_visual,
                      XCB_CW_EVENT_MASK,
                      clipboardValues);
    register_xfixes(this);
    xcb_flush(xcbConn);

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::selectionChanged,
                     data.qobject.get(),
                     [this](auto ddi) { wlSelectionChanged(ddi); });
}

void Clipboard::wlSelectionChanged(srv_data_device* ddi)
{
    if (ddi && ddi != data.srv_device) {
        // Wayland native client provides new selection
        if (!m_checkConnection) {
            m_checkConnection = QObject::connect(
                workspace(), &Workspace::clientActivated, data.qobject.get(), [this](Toplevel* ac) {
                    Q_UNUSED(ac);
                    checkWlSource();
                });
        }
        // remove previous source so checkWlSource() can create a new one
        set_wl_source(this, nullptr);
    }
    checkWlSource();
}

void Clipboard::checkWlSource()
{
    auto ddi = waylandServer()->seat()->selection();
    auto removeSource = [this] {
        if (data.wayland_source) {
            set_wl_source(this, nullptr);
            own_selection(this, false);
        }
    };

    // Wayland source gets created when:
    // - the Wl selection exists,
    // - its source is not Xwayland,
    // - a client is active,
    // - this client is an Xwayland one.
    //
    // Otherwise the Wayland source gets destroyed to shield
    // against snooping X clients.

    if (!ddi || data.srv_device == ddi) {
        // Xwayland source or no source
        QObject::disconnect(m_checkConnection);
        m_checkConnection = QMetaObject::Connection();
        removeSource();
        return;
    }
    if (!workspace()->activeClient()
        || !workspace()->activeClient()->inherits("KWin::win::x11::window")) {
        // no active client or active client is Wayland native
        removeSource();
        return;
    }
    // Xwayland client is active and we need a Wayland source
    if (data.wayland_source) {
        // source already exists, nothing more to do
        return;
    }
    auto wls = new WlSource<srv_data_device, srv_data_source>(ddi);
    set_wl_source(this, wls);
    auto* dsi = ddi->selection();
    if (dsi) {
        wls->setSourceIface(dsi);
    }
    QObject::connect(ddi,
                     &Wrapland::Server::DataDevice::selectionChanged,
                     wls->qobject(),
                     [wls](auto dsi) { wls->setSourceIface(dsi); });
    own_selection(this, true);
}

void Clipboard::doHandleXfixesNotify(xcb_xfixes_selection_notify_event_t* event)
{
    create_x11_source(this, nullptr);

    auto const& client = workspace()->activeClient();
    if (!qobject_cast<win::x11::window const*>(client)) {
        // clipboard is only allowed to be acquired when Xwayland has focus
        // TODO: can we make this stronger (window id comparison)?
        return;
    }

    create_x11_source(this, event);

    if (auto const& source = data.x11_source) {
        source->getTargets(data.requestor_window, data.atom);
    }
}

bool Clipboard::handleClientMessage([[maybe_unused]] xcb_client_message_event_t* event)
{
    return false;
}

void Clipboard::x11OffersChanged(const QStringList& added, const QStringList& removed)
{
    auto source = data.x11_source;
    if (!source) {
        return;
    }

    const Mimes offers = source->offers();

    if (!offers.isEmpty()) {
        if (!source->source() || !removed.isEmpty()) {
            // create new Wl DataSource if there is none or when types
            // were removed (Wl Data Sources can only add types)
            auto* dataDeviceManager = waylandServer()->internalDataDeviceManager();
            auto* dataSource = dataDeviceManager->createSource(source->qobject());

            // also offers directly the currently available types
            source->setSource(dataSource);
            data.clt_device->setSelection(0, dataSource);
            waylandServer()->seat()->setSelection(data.srv_device);
        } else if (auto* dataSource = source->source()) {
            for (const QString& mime : added) {
                dataSource->offer(mime);
            }
        }
    } else {
        waylandServer()->seat()->setSelection(nullptr);
    }

    waylandServer()->internalClientConection()->flush();
    waylandServer()->dispatch();
}

} // namespace Xwl
} // namespace KWin
