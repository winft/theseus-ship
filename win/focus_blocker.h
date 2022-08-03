/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::win
{

template<typename Space>
struct focus_blocker {
    focus_blocker(Space& space)
        : space{space}
    {
        space.block_focus++;
    }
    ~focus_blocker()
    {
        space.block_focus--;
    }

private:
    Space& space;
};

}
