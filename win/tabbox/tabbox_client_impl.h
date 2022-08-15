/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "tabbox_client.h"

namespace KWin
{

class Toplevel;

namespace win
{

class tabbox_client_impl : public tabbox_client
{
public:
    explicit tabbox_client_impl(Toplevel* window);
    ~tabbox_client_impl() override;

    QString caption() const override;
    QIcon icon() const override;
    bool is_minimized() const override;
    int x() const override;
    int y() const override;
    int width() const override;
    int height() const override;
    bool is_closeable() const override;
    void close() override;
    bool is_first_in_tabbox() const override;
    QUuid internal_id() const override;

    Toplevel* client() const
    {
        return m_client;
    }

private:
    Toplevel* m_client;
};

}
}
