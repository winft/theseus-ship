/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "wm_selection_owner.h"

#include <utils/memory.h>

#include <QAbstractNativeEventFilter>
#include <QBasicTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QTimerEvent>
#include <private/qtx11extras_p.h>

namespace KWin::base::backend::x11
{

xcb_atom_t wm_selection_owner::xa_version{XCB_ATOM_NONE};

wm_selection_owner::wm_selection_owner(xcb_connection_t* con, int screen)
    : selection_owner(make_selection_atom(con, screen), screen)
    , con{con}
{
}

bool wm_selection_owner::genericReply(xcb_atom_t target_P,
                                      xcb_atom_t property_P,
                                      xcb_window_t requestor_P)
{
    if (target_P == xa_version) {
        int32_t version[] = {2, 0};
        xcb_change_property(
            con, XCB_PROP_MODE_REPLACE, requestor_P, property_P, XCB_ATOM_INTEGER, 32, 2, version);
    } else
        return selection_owner::genericReply(target_P, property_P, requestor_P);
    return true;
}

void wm_selection_owner::replyTargets(xcb_atom_t property_P, xcb_window_t requestor_P)
{
    selection_owner::replyTargets(property_P, requestor_P);
    xcb_atom_t atoms[1] = {xa_version};
    // PropModeAppend !
    xcb_change_property(
        con, XCB_PROP_MODE_APPEND, requestor_P, property_P, XCB_ATOM_ATOM, 32, 1, atoms);
}

void wm_selection_owner::getAtoms()
{
    selection_owner::getAtoms();
    if (xa_version == XCB_ATOM_NONE) {
        const QByteArray name(QByteArrayLiteral("VERSION"));
        unique_cptr<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
            con, xcb_intern_atom_unchecked(con, false, name.length(), name.constData()), nullptr));
        if (atom) {
            xa_version = atom->atom;
        }
    }
}

xcb_atom_t wm_selection_owner::make_selection_atom(xcb_connection_t* con, int screen_P)
{
    if (screen_P < 0)
        screen_P = QX11Info::appScreen();
    QByteArray screen(QByteArrayLiteral("WM_S"));
    screen.append(QByteArray::number(screen_P));
    unique_cptr<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
        con, xcb_intern_atom_unchecked(con, false, screen.length(), screen.constData()), nullptr));
    if (!atom) {
        return XCB_ATOM_NONE;
    }
    return atom->atom;
}

}
