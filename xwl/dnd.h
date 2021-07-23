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
#ifndef KWIN_XWL_DND
#define KWIN_XWL_DND

#include "drag.h"
#include "selection.h"

#include <Wrapland/Client/datadevice.h>
#include <Wrapland/Client/datasource.h>
#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/data_source.h>

#include <QPoint>

namespace KWin
{
class Toplevel;

namespace Xwl
{
class Dnd;
enum class DragEventReply;

template<>
void do_handle_xfixes_notify(Dnd* sel, xcb_xfixes_selection_notify_event_t* event);
template<>
bool handle_client_message(Dnd* sel, xcb_client_message_event_t* event);

/**
 * Represents the drag and drop mechanism, on X side this is the XDND protocol.
 * For more information on XDND see: https://johnlindal.wixsite.com/xdnd
 */
class Dnd
{
    using srv_data_device = Wrapland::Server::DataDevice;
    using clt_data_device = Wrapland::Client::DataDevice;

public:
    selection_data<srv_data_device, clt_data_device> data;

    // active drag or null when no drag active
    Drag* m_currentDrag = nullptr;
    QVector<Drag*> m_oldDrags;

    explicit Dnd(xcb_atom_t atom, srv_data_device* srv_dev, clt_data_device* clt_dev);

    static uint32_t version();

    void x11OffersChanged(const QStringList& added, const QStringList& removed);

    DragEventReply dragMoveFilter(Toplevel* target, const QPoint& pos);

    Wrapland::Server::Surface* surfaceIface() const
    {
        return m_surfaceIface;
    }
    Wrapland::Client::Surface* surface() const
    {
        return m_surface;
    }

private:
    // start and end Wl native client drags (Wl -> Xwl)
    void startDrag();
    void endDrag();
    void clearOldDrag(Drag* drag);

    Wrapland::Client::Surface* m_surface;
    Wrapland::Server::Surface* m_surfaceIface = nullptr;

    Q_DISABLE_COPY(Dnd)
};

} // namespace Xwl
} // namespace KWin

#endif
