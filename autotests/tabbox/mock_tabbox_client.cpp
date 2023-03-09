/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "mock_tabbox_client.h"

#include "mock_tabbox_handler.h"

namespace KWin
{

MockTabBoxClient::MockTabBoxClient(QString const& caption)
    : tabbox_client()
    , m_caption(caption)
{
}

void MockTabBoxClient::close()
{
    static_cast<MockTabBoxHandler*>(win::tabbox_handle)->closeWindow(this);
}

} // namespace KWin
