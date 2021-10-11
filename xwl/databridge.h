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
#pragma once

#include "xwayland.h"

#include <kwin_export.h>

#include <QObject>
#include <QPoint>

#include <memory>
#include <xcb/xcb.h>

struct xcb_xfixes_selection_notify_event_t;

namespace KWin
{
class Toplevel;

namespace Xwl
{
class Clipboard;
class Dnd;
enum class DragEventReply;
class primary_selection;

/**
 * Interface class for all data sharing in the context of X selections
 * and Wayland's internal mechanism.
 *
 * Exists only once per Xwayland session.
 */
class KWIN_EXPORT DataBridge : public QObject
{
    Q_OBJECT

public:
    DataBridge(x11_data const& x11);
    ~DataBridge() override;

    bool filterEvent(xcb_generic_event_t* event);
    DragEventReply dragMoveFilter(Toplevel* target, QPoint const& pos);

private:
    bool handleXfixesNotify(xcb_xfixes_selection_notify_event_t* event);

    xcb_query_extension_reply_t const* xfixes{nullptr};

    std::unique_ptr<Clipboard> m_clipboard;
    std::unique_ptr<Dnd> m_dnd;
    std::unique_ptr<primary_selection> m_primarySelection;

    Q_DISABLE_COPY(DataBridge)
};

}
}
