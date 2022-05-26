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

namespace base::x11
{
template<typename Space>
class xcb_event_filter;
}

namespace win::wayland
{
class space;
}

class Application;

namespace xwl
{
class data_bridge;

class KWIN_EXPORT xwayland : public xwayland_interface
{
    Q_OBJECT

public:
    /** The @ref status_callback is called once with 0 code when Xwayland is ready, other codes
     *  indicate a critical error happened at runtime.
     */
    xwayland(Application* app,
             win::wayland::space& space,
             std::function<void(int code)> status_callback);
    ~xwayland() override;

    std::unique_ptr<xwl::data_bridge> data_bridge;

private:
    void continue_startup_with_x11();

    drag_event_reply drag_move_filter(Toplevel* target, QPoint const& pos) override;

    int xcb_connection_fd{-1};
    QProcess* xwayland_process{nullptr};
    QMetaObject::Connection xwayland_fail_notifier;

    x11_data basic_data;

    std::unique_ptr<QSocketNotifier> xcb_read_notifier;
    std::unique_ptr<base::x11::xcb_event_filter<win::wayland::space>> event_filter;

    Application* app;
    win::wayland::space& space;
    std::function<void(int code)> status_callback;

    Q_DISABLE_COPY(xwayland)
};

}
}
