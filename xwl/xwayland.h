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
#pragma once

#include "types.h"

#include <memory>
#include <xcb/xproto.h>

class QProcess;
class QSocketNotifier;

class xcb_screen_t;

namespace KWin
{
class ApplicationWaylandAbstract;

namespace Xwl
{
class DataBridge;

class KWIN_EXPORT Xwayland : public XwaylandInterface
{
    Q_OBJECT

public:
    /** The @ref status_callback is called once with 0 code when Xwayland is ready, other codes
     *  indicate a critical error happened at runtime.
     */
    Xwayland(ApplicationWaylandAbstract* app, std::function<void(int code)> status_callback);
    ~Xwayland() override;

    std::unique_ptr<DataBridge> data_bridge;

private:
    void continue_startup_with_x11();

    DragEventReply drag_move_filter(Toplevel* target, QPoint const& pos) override;

    int m_xcbConnectionFd = -1;
    QProcess* m_xwaylandProcess = nullptr;
    QMetaObject::Connection m_xwaylandFailConnection;

    x11_data basic_data;

    std::unique_ptr<QSocketNotifier> xcb_read_notifier;

    ApplicationWaylandAbstract* m_app;
    std::function<void(int code)> status_callback;

    Q_DISABLE_COPY(Xwayland)
};

}
}
