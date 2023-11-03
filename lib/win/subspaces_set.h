/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/subspace.h>

#include <QAction>
#include <cassert>

namespace KWin::win
{

template<typename Manager>
bool subspaces_set_current(Manager& mgr, subspace& subsp)
{
    if (mgr.current == &subsp) {
        return false;
    }

    auto old_subsp = mgr.current;
    mgr.current = &subsp;

    Q_EMIT mgr.qobject->current_changed(old_subsp, mgr.current);
    return true;
}

template<typename Manager>
bool subspaces_set_current(Manager& mgr, uint x11id)
{
    if (x11id < 1 || x11id > mgr.subspaces.size()) {
        return false;
    }

    auto subsp = subspaces_get_for_x11id(mgr, x11id);
    assert(subsp);
    return subspaces_set_current(mgr, *subsp);
}

template<typename Manager>
void subspaces_set_current(Manager& mgr, QAction& action)
{
    auto ok = false;
    auto const x11id = action.data().toUInt(&ok);
    if (ok) {
        subspaces_set_current(mgr, x11id);
    }
}

}
