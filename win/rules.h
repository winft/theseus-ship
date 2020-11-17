/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::win
{

template<typename Win>
void finish_rules(Win* win)
{
    win->updateWindowRules(Rules::All);
    win->control()->set_rules(WindowRules());
}

}
