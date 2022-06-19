/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "remnant.h"

#include "control.h"
#include "deco/renderer.h"
#include "geo.h"
#include "meta.h"
#include "net.h"
#include "space_helpers.h"
#include "transient.h"
#include "x11/window.h"

#include "base/logging.h"

#include <cassert>

namespace KWin::win
{

remnant::remnant(Toplevel* win)
    : win{win}
{
}

remnant::~remnant()
{
    if (refcount != 0) {
        qCCritical(KWIN_CORE) << "Deleted client has non-zero reference count (" << refcount << ")";
    }
    assert(refcount == 0);
    delete_window_from_space(win->space, win);
}

void remnant::ref()
{
    ++refcount;
}

void remnant::unref()
{
    --refcount;
}

void remnant::discard()
{
    refcount = 0;
    delete win;
}

bool remnant::was_transient() const
{
    return win->transient()->lead();
}

bool remnant::has_lead(Toplevel const* toplevel) const
{
    return contains(win->transient()->leads(), toplevel);
}

void remnant::layout_decoration_rects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
{
    left = decoration_left;
    top = decoration_top;
    right = decoration_right;
    bottom = decoration_bottom;
}

}
