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
    if (space.tabbox->is_displayed()) {
        space.tabbox->reset(true);
    }
#endif
}

}
