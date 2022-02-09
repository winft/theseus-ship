/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/proto.h"
#include "kwin_export.h"

#include <deque>
#include <memory>

namespace KWin
{
class Toplevel;

namespace win::x11
{

class KWIN_EXPORT stacking_tree
{
public:
    std::deque<Toplevel*> const& as_list();
    void mark_as_dirty();

private:
    void update();

    std::deque<Toplevel*> winlist;
    std::unique_ptr<base::x11::xcb::tree> xcbtree;
    bool is_dirty{false};
};

}
}
