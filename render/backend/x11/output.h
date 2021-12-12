/*
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin
{

namespace base::x11
{
class output;
}

namespace render::backend::x11
{

class output
{
public:
    output(base::x11::output const& base)
        : base{base}
    {
    }

    base::x11::output const& base;
};

}
}
