/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::render::backend::x11
{

template<typename Base>
class output
{
public:
    output(Base const& base)
        : base{base}
    {
    }

    Base const& base;
};

}
