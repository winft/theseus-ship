/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "atoms_p.h"

#include "info_p.h"

#include <QHash>
#include <xcb/xproto.h>

namespace KWin::win::x11::net
{

typedef QHash<xcb_connection_t*, QSharedDataPointer<Atoms>> AtomHash;
Q_GLOBAL_STATIC(AtomHash, s_gAtomsHash)

QSharedDataPointer<Atoms> atomsForConnection(xcb_connection_t* c)
{
    auto it = s_gAtomsHash->constFind(c);
    if (it == s_gAtomsHash->constEnd()) {
        QSharedDataPointer<Atoms> atom(new Atoms(c));
        s_gAtomsHash->insert(c, atom);
        return atom;
    }
    return it.value();
}

Atoms::Atoms(xcb_connection_t* c)
    : QSharedData()
    , m_connection(c)
{
    for (int i = 0; i < KwsAtomCount; ++i) {
        m_atoms[i] = XCB_ATOM_NONE;
    }
    init();
}

void Atoms::init()
{
#define ENUM_CREATE_CHAR_ARRAY 1
#include "atoms_p.h" // creates const char* array "KwsAtomStrings"
    // Send the intern atom requests
    xcb_intern_atom_cookie_t cookies[KwsAtomCount];
    for (int i = 0; i < KwsAtomCount; ++i) {
        cookies[i]
            = xcb_intern_atom(m_connection, false, strlen(KwsAtomStrings[i]), KwsAtomStrings[i]);
    }

    // Get the replies
    for (int i = 0; i < KwsAtomCount; ++i) {
        xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(m_connection, cookies[i], nullptr);
        if (!reply) {
            continue;
        }

        m_atoms[i] = reply->atom;
        free(reply);
    }
}

void reset_atoms()
{
    s_gAtomsHash->clear();
}

}
