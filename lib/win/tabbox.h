/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

namespace KWin::win
{

template<typename Space>
void update_tabbox(Space& space)
{
#if KWIN_BUILD_TABBOX
    // Need to reset the client model even if the task switcher is hidden otherwise there
    // might be dangling pointers. Consider rewriting client model logic!
    space.tabbox->reset(true);
#endif
}

}
