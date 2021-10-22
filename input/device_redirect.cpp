/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device_redirect.h"

#include "decorations/decoratedclient.h"
#include "toplevel.h"

namespace KWin::input
{

device_redirect::device_redirect()
    : QObject()
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

}
