/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device_redirect.h"

#include "decorations/decoratedclient.h"
#include "toplevel.h"

namespace KWin::input
{

device_redirect::device_redirect(input::redirect* redirect)
    : redirect{redirect}
{
}

device_redirect::~device_redirect() = default;

Toplevel* device_redirect::at() const
{
    return m_at.at.data();
}

Toplevel* device_redirect::focus() const
{
    return m_focus.focus.data();
}

Decoration::DecoratedClientImpl* device_redirect::decoration() const
{
    return m_focus.decoration;
}

QWindow* device_redirect::internalWindow() const
{
    return m_focus.internalWindow;
}

QPointF device_redirect::position() const
{
    return {};
}

void device_redirect::cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
{
}
void device_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* /*old*/,
                                        Decoration::DecoratedClientImpl* /*now*/)
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

bool device_redirect::inited() const
{
    return m_inited;
}

void device_redirect::setInited(bool set)
{
    m_inited = set;
}

}
