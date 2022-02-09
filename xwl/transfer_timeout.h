/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_data.h"

#include "win/x11/window.h"

#include <QObject>
#include <QTimer>

namespace KWin::xwl
{

// Time out transfers, which have become inactive due to client errors.
template<typename Selection>
void timeout_transfers(Selection* sel)
{
    for (auto& transfer : sel->data.transfers.x11_to_wl) {
        transfer->timeout();
    }
    for (auto& transfer : sel->data.transfers.wl_to_x11) {
        transfer->timeout();
    }
}

template<typename Selection>
void start_timeout_transfers_timer(Selection* sel)
{
    if (sel->data.transfers.timeout) {
        return;
    }
    sel->data.transfers.timeout = new QTimer(sel->data.qobject.get());
    QObject::connect(sel->data.transfers.timeout,
                     &QTimer::timeout,
                     sel->data.qobject.get(),
                     [sel]() { timeout_transfers(sel); });
    sel->data.transfers.timeout->start(5000);
}

template<typename Selection>
void end_timeout_transfers_timer(Selection* sel)
{
    if (sel->data.transfers.x11_to_wl.empty() && sel->data.transfers.wl_to_x11.empty()) {
        delete sel->data.transfers.timeout;
        sel->data.transfers.timeout = nullptr;
    }
}

}
