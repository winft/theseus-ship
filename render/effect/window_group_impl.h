/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"
#include "win/x11/group.h"

#include <kwineffects/effect_window.h>

#include <QHash>

namespace KWin::render
{

class effect_window_group_impl : public EffectWindowGroup
{
public:
    explicit effect_window_group_impl(win::x11::group* g)
        : group(g)
    {
    }

    EffectWindowList members() const override
    {
        const auto memberList = group->members;
        EffectWindowList ret;
        ret.reserve(memberList.size());
        std::transform(std::cbegin(memberList),
                       std::cend(memberList),
                       std::back_inserter(ret),
                       [](auto win) { return win->render->effect.get(); });
        return ret;
    }

private:
    win::x11::group* group;
};

}
