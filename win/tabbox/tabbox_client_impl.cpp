/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tabbox_client_impl.h"

#include "toplevel.h"
#include "win/meta.h"

namespace KWin::win
{

tabbox_client_impl::tabbox_client_impl(Toplevel* window)
    : tabbox_client()
    , m_client(window)
{
}

tabbox_client_impl::~tabbox_client_impl()
{
}

QString tabbox_client_impl::caption() const
{
    if (win::is_desktop(m_client)) {
        return i18nc("Special entry in alt+tab list for minimizing all windows", "Show Desktop");
    }
    return win::caption(m_client);
}

QIcon tabbox_client_impl::icon() const
{
    if (win::is_desktop(m_client)) {
        return QIcon::fromTheme(QStringLiteral("user-desktop"));
    }
    return m_client->control->icon;
}

bool tabbox_client_impl::is_minimized() const
{
    return m_client->control->minimized;
}

int tabbox_client_impl::x() const
{
    return m_client->pos().x();
}

int tabbox_client_impl::y() const
{
    return m_client->pos().y();
}

int tabbox_client_impl::width() const
{
    return m_client->size().width();
}

int tabbox_client_impl::height() const
{
    return m_client->size().height();
}

bool tabbox_client_impl::is_closeable() const
{
    return m_client->isCloseable();
}

void tabbox_client_impl::close()
{
    m_client->closeWindow();
}

bool tabbox_client_impl::is_first_in_tabbox() const
{
    return m_client->control->first_in_tabbox;
}

QUuid tabbox_client_impl::internal_id() const
{
    return m_client->internal_id;
}

}
