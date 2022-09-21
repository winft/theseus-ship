/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "tabbox_client.h"

#include "win/meta.h"

namespace KWin::win
{

template<typename Window>
class tabbox_client_impl : public tabbox_client
{
public:
    explicit tabbox_client_impl(Window* window)
        : m_client(window)
    {
    }

    QString caption() const override
    {
        if (win::is_desktop(m_client)) {
            return i18nc("Special entry in alt+tab list for minimizing all windows",
                         "Show Desktop");
        }
        return win::caption(m_client);
    }

    QIcon icon() const override
    {
        if (win::is_desktop(m_client)) {
            return QIcon::fromTheme(QStringLiteral("user-desktop"));
        }
        return m_client->control->icon;
    }

    bool is_minimized() const override
    {
        return m_client->control->minimized;
    }

    int x() const override
    {
        return m_client->geo.pos().x();
    }

    int y() const override
    {
        return m_client->geo.pos().y();
    }

    int width() const override
    {
        return m_client->geo.size().width();
    }

    int height() const override
    {
        return m_client->geo.size().height();
    }

    bool is_closeable() const override
    {
        return m_client->isCloseable();
    }

    void close() override
    {
        m_client->closeWindow();
    }

    bool is_first_in_tabbox() const override
    {
        return m_client->control->first_in_tabbox;
    }

    QUuid internal_id() const override
    {
        return m_client->meta.internal_id;
    }

    Window* client() const
    {
        return m_client;
    }

private:
    Window* m_client;
};

}
