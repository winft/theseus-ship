/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <deque>
#include <memory>

namespace KWin
{
class Toplevel;

namespace win
{

class space;

namespace x11
{

class KWIN_EXPORT stacking_tree
{
public:
    stacking_tree(win::space& space);

private:
    void update();

    win::space& space;
};

}
}
}
