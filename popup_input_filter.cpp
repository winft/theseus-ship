/*
 * Copyright 2017  Martin Graesslin <mgraesslin@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "popup_input_filter.h"
#include "wayland_server.h"

#include "win/deco.h"
#include "win/geo.h"
#include "win/transient.h"
#include "win/util.h"
#include "win/wayland/window.h"

#include <QMouseEvent>

namespace KWin
{

PopupInputFilter::PopupInputFilter()
    : QObject()
{
    connect(waylandServer(), &WaylandServer::window_added, this, &PopupInputFilter::handle_window_added);
}

void PopupInputFilter::handle_window_added(win::wayland::window *window)
{
    if (contains(m_popups, window)) {
        return;
    }
    if (window->transient()->input_grab) {
        // TODO: verify that the Toplevel is allowed as a popup
        connect(window, &Toplevel::windowShown,
                this, [this, window] { handle_window_added(window); }, Qt::UniqueConnection);
        connect(window, &Toplevel::windowClosed,
                this, &PopupInputFilter::handle_window_removed, Qt::UniqueConnection);
        m_popups.push_back(window);
    }
}

void PopupInputFilter::handle_window_removed(Toplevel* window)
{
    remove_all(m_popups, window);
}
bool PopupInputFilter::pointerEvent(QMouseEvent *event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton)
    if (m_popups.empty()) {
        return false;
    }
    if (event->type() == QMouseEvent::MouseButtonPress) {
        auto focus_window = input_redirect()->findToplevel(event->globalPos());
        if (!focus_window || !win::belong_to_same_client(focus_window, m_popups.back())) {
            // a press on a window (or no window) not belonging to the popup window
            cancelPopups();
            // filter out this press
            return true;
        }
        if (focus_window && win::decoration(focus_window)) {
            // Test whether it is on the decoration.
            auto const content_rect
                = focus_window->frameGeometry() - win::frame_margins(focus_window);
            if (!content_rect.contains(event->globalPos())) {
                cancelPopups();
                return true;
            }
        }
    }
    return false;
}

void PopupInputFilter::cancelPopups()
{
    while (!m_popups.empty()) {
        m_popups.back()->cancel_popup();
        m_popups.pop_back();
    }
}

}
