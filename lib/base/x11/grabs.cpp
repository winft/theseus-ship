/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "grabs.h"

#include "base/logging.h"
#include "utils/memory.h"

#include <QApplication>
#include <QWidget>
#include <cassert>

namespace KWin::base::x11
{

static int server_grab_count = 0;

void grab_server(xcb_connection_t* con)
{
    if (++server_grab_count == 1) {
        xcb_grab_server(con);
    }
}

void ungrab_server(xcb_connection_t* con)
{
    assert(server_grab_count > 0);
    if (--server_grab_count == 0) {
        xcb_ungrab_server(con);
        xcb_flush(con);
    }
}

}
