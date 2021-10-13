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

#include <kwinglobals.h>

#include <QObject>
#include <QPoint>

namespace KWin
{
class Toplevel;

namespace xwl
{
enum class DragEventReply {
    // event should be ignored by the filter
    Ignore,
    // event is filtered out
    Take,
    // event should be handled as a Wayland native one
    Wayland,
};

class KWIN_EXPORT XwaylandInterface : public QObject
{
    Q_OBJECT

public:
    static XwaylandInterface* self();

    virtual DragEventReply drag_move_filter(Toplevel* target, QPoint const& pos) = 0;

protected:
    XwaylandInterface();
    ~XwaylandInterface() override;

private:
    Q_DISABLE_COPY(XwaylandInterface)
};

}

inline xwl::XwaylandInterface* xwayland()
{
    return xwl::XwaylandInterface::self();
}

}
