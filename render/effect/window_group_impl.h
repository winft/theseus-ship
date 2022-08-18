/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <kwineffects/effect_window.h>

namespace KWin::render
{

template<typename Group>
class effect_window_group_impl : public EffectWindowGroup
{
public:
    explicit effect_window_group_impl(Group* group)
        : group(group)
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
    Group* group;
};

}
