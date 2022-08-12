/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QObject>
#include <memory>

class QTimer;

namespace KWin::xwl
{
class wl_to_x11_transfer;
class x11_to_wl_transfer;

template<typename>
class wl_source;
template<typename>
class x11_source;

/*
 * QObject attribute of a Selection.
 * This is a hack around having a template QObject.
 */
class KWIN_EXPORT q_selection : public QObject
{
    Q_OBJECT

public:
Q_SIGNALS:
    void transfer_finished(xcb_timestamp_t eventTime);
};

/**
 * Data needed by X selections and their Wayland counter-parts.
 *
 * A selection should exist through the whole runtime of an Xwayland
 * session.
 * Each selection holds an independent instance of this class,
 * containing the source and the active transfers.
 *
 * This class can be specialized to support the core Wayland protocol
 * (clipboard and dnd) as well as primary selection.
 */
template<typename server_source, typename internal_source>
struct selection_data {
    std::unique_ptr<q_selection> qobject;

    xcb_atom_t atom{XCB_ATOM_NONE};
    xcb_window_t window{XCB_WINDOW_NONE};

    bool disown_pending{false};
    xcb_timestamp_t timestamp{XCB_CURRENT_TIME};
    xcb_window_t requestor_window{XCB_WINDOW_NONE};

    // Active source, if any. Only one of them at max can exist
    // at the same time.
    std::unique_ptr<wl_source<server_source>> wayland_source;
    std::unique_ptr<xwl::x11_source<internal_source>> x11_source;

    std::unique_ptr<internal_source> source_int;

    x11_data x11;
    QMetaObject::Connection active_window_notifier;

    // active transfers
    struct {
        std::vector<wl_to_x11_transfer*> wl_to_x11;
        std::vector<x11_to_wl_transfer*> x11_to_wl;
        QTimer* timeout{nullptr};
    } transfers;

    selection_data() = default;
    selection_data(selection_data const&) = delete;
    selection_data& operator=(selection_data const&) = delete;
    selection_data(selection_data&&) noexcept = default;
    selection_data& operator=(selection_data&&) noexcept = default;
    ~selection_data() = default;
};

template<typename server_source, typename internal_source>
auto create_selection_data(xcb_atom_t atom, x11_data const& x11)
{
    selection_data<server_source, internal_source> sel;

    sel.qobject.reset(new q_selection());
    sel.atom = atom;
    sel.x11 = x11;

    sel.window = xcb_generate_id(x11.connection);
    sel.requestor_window = sel.window;
    xcb_flush(x11.connection);

    return sel;
}

}
