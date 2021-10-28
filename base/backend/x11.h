/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::base
{
class output;

namespace backend
{

class x11
{
public:
    using output = base::output;

    x11() = default;
    x11(x11 const&) = delete;
    x11& operator=(x11 const&) = delete;
    x11(x11&& other) noexcept = default;
    x11& operator=(x11&& other) noexcept = default;
    ~x11() = default;
};

}
}
