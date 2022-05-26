/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device_redirect.h"

#include "toplevel.h"
#include "win/deco/client_impl.h"

namespace KWin::input
{

device_redirect::device_redirect(input::redirect* redirect)
    : redirect{redirect}
{
}

device_redirect::~device_redirect() = default;

QPointF device_redirect::position() const
{
    return {};
}

void device_redirect::cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
{
}
void device_redirect::cleanupDecoration(win::deco::client_impl* /*old*/,
                                        win::deco::client_impl* /*now*/)
{
}

void device_redirect::focusUpdate(Toplevel* /*old*/, Toplevel* /*now*/)
{
}

bool device_redirect::positionValid() const
{
    return true;
}

bool device_redirect::focusUpdatesBlocked()
{
    return false;
}

}
