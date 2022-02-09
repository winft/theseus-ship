/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/memory.h"

#include <QByteArray>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

class atom
{
public:
    atom(QByteArray const& name, xcb_connection_t* connection)
        : atom(name, false, connection)
    {
    }
    atom(QByteArray const& name, bool only_if_exists, xcb_connection_t* connection)
        : m_connection(connection)
        , m_cookie(xcb_intern_atom_unchecked(connection,
                                             only_if_exists,
                                             name.length(),
                                             name.constData()))
        , m_name(name)
    {
    }

    atom(atom const& other)
    {
        *this = other;
    }

    atom& operator=(atom const& other)
    {
        if (this == &other) {
            return *this;
        }

        m_connection = other.m_connection;
        m_name = other.m_name;
        m_retrieved = other.m_retrieved;

        if (m_retrieved) {
            m_atom = other.m_atom;
            m_cookie.sequence = 0;
        } else {
            // Set only_if_exists to true because the other has already created it if false.
            m_cookie = xcb_intern_atom_unchecked(
                m_connection, true, m_name.length(), m_name.constData());
        }

        return *this;
    }

    atom(atom&& other) noexcept = default;
    atom& operator=(atom&& other) noexcept = default;

    ~atom()
    {
        if (!m_retrieved && m_cookie.sequence) {
            xcb_discard_reply(m_connection, m_cookie.sequence);
        }
    }

    operator xcb_atom_t() const
    {
        (const_cast<atom*>(this))->get_reply();
        return m_atom;
    }
    bool is_valid()
    {
        get_reply();
        return m_atom != XCB_ATOM_NONE;
    }
    bool is_valid() const
    {
        (const_cast<atom*>(this))->get_reply();
        return m_atom != XCB_ATOM_NONE;
    }

    inline const QByteArray& name() const
    {
        return m_name;
    }

private:
    void get_reply()
    {
        if (m_retrieved || !m_cookie.sequence) {
            return;
        }
        unique_cptr<xcb_intern_atom_reply_t> reply(
            xcb_intern_atom_reply(m_connection, m_cookie, nullptr));
        if (reply) {
            m_atom = reply->atom;
        }
        m_retrieved = true;
    }
    xcb_connection_t* m_connection;
    bool m_retrieved{false};
    xcb_intern_atom_cookie_t m_cookie;
    xcb_atom_t m_atom{XCB_ATOM_NONE};
    QByteArray m_name;
};

}
