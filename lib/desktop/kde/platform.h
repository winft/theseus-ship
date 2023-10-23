/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <desktop/kde/dbus/kwin.h>
#include <desktop/platform.h>

namespace KWin::desktop::kde
{

template<typename Space>
class platform : public desktop::platform
{
public:
    explicit platform(Space& space)
        : desktop::platform(space)
        , dbus{std::make_unique<kde::kwin_impl<Space>>(space)}
    {
    }

    std::unique_ptr<kde::kwin_impl<Space>> dbus;
};

}
