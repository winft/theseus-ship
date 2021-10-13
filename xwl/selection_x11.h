/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event_x11.h"
#include "selection_source.h"
#include "sources.h"
#include "transfer.h"
#include "transfer_timeout.h"
#include "types.h"

#include <QObject>

#include <xcb/xfixes.h>

namespace KWin::xwl
{

template<typename Selection>
void create_x11_source(Selection* sel, xcb_xfixes_selection_notify_event_t* event)
{
    assert(event);
    assert(!sel->data.x11_source);

    if (event->owner == XCB_WINDOW_NONE) {
        return;
    }

    // We may remove a current Wayland selection at this point.
    sel->data.wayland_source.reset();

    using internal_source = std::remove_pointer_t<decltype(sel->data.source_int.get())>;
    sel->data.x11_source.reset(new x11_source<internal_source>(event, sel->data.x11));

    QObject::connect(sel->data.x11_source->get_qobject(),
                     &q_x11_source::offers_changed,
                     sel->data.qobject.get(),
                     [sel](auto const& added, auto const& removed) {
                         handle_x11_offer_change(sel, added, removed);
                     });
    QObject::connect(sel->data.x11_source->get_qobject(),
                     &q_x11_source::transfer_ready,
                     sel->data.qobject.get(),
                     [sel](auto target, auto fd) { start_transfer_to_wayland(sel, target, fd); });
}

template<typename Selection>
void start_transfer_to_wayland(Selection* sel, xcb_atom_t target, qint32 fd)
{
    auto transfer = new x11_to_wl_transfer(sel->data.atom,
                                           target,
                                           fd,
                                           sel->data.x11_source->get_timestamp(),
                                           sel->data.requestor_window,
                                           sel->data.x11,
                                           sel->data.qobject.get());
    sel->data.transfers.x11_to_wl.push_back(transfer);

    QObject::connect(
        transfer, &x11_to_wl_transfer::finished, sel->data.qobject.get(), [sel, transfer]() {
            Q_EMIT sel->data.qobject->transfer_finished(transfer->get_timestamp());
            delete transfer;
            remove_all(sel->data.transfers.x11_to_wl, transfer);
            end_timeout_transfers_timer(sel);
        });

    start_timeout_transfers_timer(sel);
}

template<typename Selection>
void handle_x11_offer_change(Selection* sel,
                             std::vector<std::string> const& added,
                             std::vector<std::string> const& removed)
{
    using internal_source = std::remove_pointer_t<decltype(sel->data.source_int.get())>;

    if (!sel->data.x11_source) {
        return;
    }

    auto const offers = sel->data.x11_source->get_offers();
    if (offers.empty()) {
        sel->set_selection(nullptr);
        return;
    }

    if (!sel->data.x11_source->get_source() || !removed.empty()) {
        // create new Wl DataSource if there is none or when types
        // were removed (Wl Data Sources can only add types)
        auto old_source_int = sel->data.source_int.release();

        sel->data.source_int.reset(new internal_source());
        sel->data.x11_source->set_source(sel->data.source_int.get());
        sel->set_selection(sel->data.source_int->src());

        // Delete old internal source after setting the new one so data-control devices won't
        // receive an intermediate null selection and send it back to us overriding our new one.
        delete old_source_int;
    } else if (auto dataSource = sel->data.x11_source->get_source()) {
        for (auto const& mime : added) {
            dataSource->offer(mime);
        }
    }
}

template<typename Selection>
void register_xfixes(Selection* sel)
{
    auto xcb_con = kwinApp()->x11Connection();

    uint32_t const mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
        | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;

    xcb_xfixes_select_selection_input(
        kwinApp()->x11Connection(), sel->data.window, sel->data.atom, mask);
    xcb_flush(xcb_con);
}

template<typename Selection>
void register_x11_selection(Selection* sel, QSize const& window_size)
{
    auto xcb_con = sel->data.x11.connection;

    uint32_t const values[] = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE};
    xcb_create_window(xcb_con,
                      XCB_COPY_FROM_PARENT,
                      sel->data.window,
                      kwinApp()->x11RootWindow(),
                      0,
                      0,
                      window_size.width(),
                      window_size.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      sel->data.x11.screen->root_visual,
                      XCB_CW_EVENT_MASK,
                      values);
    register_xfixes(sel);
    xcb_flush(xcb_con);
}

}
