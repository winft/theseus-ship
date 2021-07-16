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
    std::unique_ptr<q_selection> qobject;

    xcb_atom_t atom{XCB_ATOM_NONE};
    xcb_window_t window{XCB_WINDOW_NONE};

    bool disown_pending{false};
    xcb_timestamp_t timestamp;
    xcb_window_t requestor_window{XCB_WINDOW_NONE};

    // Active source, if any. Only one of them at max can exist
    // at the same time.
    WlSource<srv_data_device, srv_data_source>* wayland_source{nullptr};
    X11Source<clt_data_source>* x11_source{nullptr};

    // active transfers
    struct {
        QVector<TransferWltoX*> wl_to_x11;
        QVector<TransferXtoWl*> x11_to_wl;
        QTimer* timeout{nullptr};
    } transfers;

    ~Selection();

protected:
    Selection(xcb_atom_t atom);
};

} // namespace Xwl
} // namespace KWin

#endif
