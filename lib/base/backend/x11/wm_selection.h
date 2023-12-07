/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/logging.h>
#include <base/x11/xcb/helpers.h>

#include <QCoreApplication>

namespace KWin::base::backend::x11
{

template<typename Platform>
void wm_selection_handle_loss(Platform& platform)
{
    qApp->sendPostedEvents();

    platform.mod.space = {};
    platform.mod.render = {};

    // Remove windowmanager privileges
    base::x11::xcb::select_input(platform.x11_data.connection,
                                 platform.x11_data.root_window,
                                 XCB_EVENT_MASK_PROPERTY_CHANGE);
    qApp->quit();
}

template<typename Platform>
void wm_selection_owner_create(Platform& platform)
{
    using wm_owner_t = decltype(platform.owner)::element_type;

    platform.owner = std::make_unique<wm_owner_t>(platform.x11_data.connection,
                                                  platform.x11_data.screen_number);

    QObject::connect(platform.owner.get(), &wm_owner_t::failedToClaimOwnership, [] {
        qCCritical(KWIN_CORE,
                   "Unable to claim manager selection, another wm running? (try using "
                   "--replace)\n");
        ::exit(1);
    });
    QObject::connect(platform.owner.get(),
                     &wm_owner_t::lostOwnership,
                     platform.qobject.get(),
                     [&platform] { wm_selection_handle_loss(platform); });
}

}
