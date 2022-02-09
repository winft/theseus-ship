/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <cstdlib>
#include <memory>

namespace KWin
{

struct free_deleter {
    template<typename T>
    void operator()(T* p) const
    {
        std::free(const_cast<std::remove_const_t<T>*>(p));
    }
};

// Smart pointer for memory allocated with malloc and deleted with free.
template<typename T>
using unique_cptr = std::unique_ptr<T, free_deleter>;

}
