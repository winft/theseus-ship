/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "atoms_p.h"
#include "geo.h"
#include "net.h"
#include "rarray.h"

#include "kwin_export.h"

#include <QSharedData>
#include <QSharedDataPointer>
#include <xcb/xcb.h>

namespace KWin::win::x11::net
{

class KWIN_EXPORT Atoms : public QSharedData
{
public:
    explicit Atoms(xcb_connection_t* c);

    xcb_atom_t atom(KwsAtom atom) const
    {
        return m_atoms[atom];
    }

private:
    void init();
    xcb_atom_t m_atoms[KwsAtomCount];
    xcb_connection_t* m_connection;
};

KWIN_EXPORT QSharedDataPointer<Atoms> atomsForConnection(xcb_connection_t* c);

template<typename T>
T get_value_reply(xcb_connection_t* c,
                  const xcb_get_property_cookie_t cookie,
                  xcb_atom_t type,
                  T def,
                  bool* success = nullptr)
{
    T value = def;

    xcb_get_property_reply_t* reply = xcb_get_property_reply(c, cookie, nullptr);

    if (success) {
        *success = false;
    }

    if (reply) {
        if (reply->type == type && reply->value_len == 1 && reply->format == sizeof(T) * 8) {
            value = *reinterpret_cast<T*>(xcb_get_property_value(reply));

            if (success) {
                *success = true;
            }
        }

        free(reply);
    }

    return value;
}

template<typename T>
QVector<T>
get_array_reply(xcb_connection_t* c, const xcb_get_property_cookie_t cookie, xcb_atom_t type)
{
    xcb_get_property_reply_t* reply = xcb_get_property_reply(c, cookie, nullptr);
    if (!reply) {
        return QVector<T>();
    }

    QVector<T> vector;

    if (reply->type == type && reply->value_len > 0 && reply->format == sizeof(T) * 8) {
        T* data = reinterpret_cast<T*>(xcb_get_property_value(reply));

        vector.resize(reply->value_len);
        memcpy((void*)&vector.first(), (void*)data, reply->value_len * sizeof(T));
    }

    free(reply);
    return vector;
}

QByteArray
get_string_reply(xcb_connection_t* c, const xcb_get_property_cookie_t cookie, xcb_atom_t type);

QList<QByteArray>
get_stringlist_reply(xcb_connection_t* c, const xcb_get_property_cookie_t cookie, xcb_atom_t type);

void send_client_message(xcb_connection_t* c,
                         uint32_t mask,
                         xcb_window_t destination,
                         xcb_window_t window,
                         xcb_atom_t message,
                         uint32_t const data[]);

}
