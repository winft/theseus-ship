/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2018 Roman Gilg <subdiff@gmail.com>

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
#include "databridge.h"
#include "clipboard.h"
#include "dnd.h"
#include "selection.h"
#include "xwayland.h"

#include "atoms.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include <Wrapland/Client/datadevicemanager.h>
#include <Wrapland/Client/seat.h>

#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/data_device_manager.h>
#include <Wrapland/Server/seat.h>

using namespace Wrapland::Client;

namespace KWin
{
namespace Xwl
{

static DataBridge* s_self = nullptr;

DataBridge* DataBridge::self()
{
    return s_self;
}

DataBridge::DataBridge(QObject* parent)
    : QObject(parent)
{
    s_self = this;

    m_dataDevice = waylandServer()->internalDataDeviceManager()->getDevice(
        waylandServer()->internalSeat(), this);
    waylandServer()->dispatch();

    auto* dc = new QMetaObject::Connection();
    *dc = connect(waylandServer()->dataDeviceManager(),
                  &Wrapland::Server::DataDeviceManager::deviceCreated,
                  this,
                  [this, dc](Wrapland::Server::DataDevice* dataDeviceInterface) {
                      if (dataDeviceInterface->client() != waylandServer()->internalConnection()) {
                          return;
                      }

                      QObject::disconnect(*dc);
                      delete dc;

                      assert(!m_dataDeviceInterface);
                      m_dataDeviceInterface = dataDeviceInterface;

                      assert(!m_clipboard);
                      assert(!m_dnd);
                      m_clipboard.reset(new Clipboard(atoms->clipboard));
                      m_dnd.reset(new Dnd(atoms->xdnd_selection));

                      waylandServer()->dispatch();
                  });
}

DataBridge::~DataBridge()
{
    s_self = nullptr;
}

bool DataBridge::filterEvent(xcb_generic_event_t* event)
{
    if (filter_event(m_clipboard.get(), event)) {
        return true;
    }
    if (filter_event(m_dnd.get(), event)) {
        return true;
    }
    if (event->response_type - Xwayland::self()->xfixes()->first_event
        == XCB_XFIXES_SELECTION_NOTIFY) {
        return handleXfixesNotify((xcb_xfixes_selection_notify_event_t*)event);
    }
    return false;
}

bool DataBridge::handleXfixesNotify(xcb_xfixes_selection_notify_event_t* event)
{
    if (event->selection == atoms->clipboard) {
        return handle_xfixes_notify(m_clipboard.get(), event);
    }
    if (event->selection == atoms->xdnd_selection) {
        return handle_xfixes_notify(m_dnd.get(), event);
    }
    return false;
}

DragEventReply DataBridge::dragMoveFilter(Toplevel* target, const QPoint& pos)
{
    if (!m_dnd) {
        return DragEventReply::Wayland;
    }
    return m_dnd->dragMoveFilter(target, pos);
}

} // namespace Xwl
} // namespace KWin
