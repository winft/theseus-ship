/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::win::x11
{

template<typename Space>
void reset_update_tool_windows_timer(Space& space)
{
    space.updateToolWindowsTimer.start(200);
}

}
