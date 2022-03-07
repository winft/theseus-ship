/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: MIT
*/
#pragma once

#include <algorithm>

namespace KWin
{

template<typename V, typename T>
auto find(V const& container, T const& arg)
{
    return std::find(container.begin(), container.end(), arg);
}

template<typename V, typename T>
int index_of(V const& container, T const& arg)
{
    auto it = std::find(container.cbegin(), container.cend(), arg);
    if (it == container.cend()) {
        return -1;
    }
    return it - container.cbegin();
}

template<typename V, typename T>
bool contains(V const& container, T const& arg)
{
    return std::find(container.cbegin(), container.cend(), arg) != container.cend();
}

template<typename V, typename F>
bool contains_if(V const& container, F&& f)
{
    return std::find_if(container.cbegin(), container.cend(), f) != container.cend();
}

template<typename V, typename T>
void remove_all(V& container, T const& arg)
{
    container.erase(std::remove(container.begin(), container.end(), arg), container.end());
}

template<typename V, typename F>
void remove_all_if(V& container, F&& f)
{
    container.erase(std::remove_if(container.begin(), container.end(), f), container.end());
}

template<typename Enum>
constexpr auto enum_index(Enum enumerator) noexcept
{
    return static_cast<std::underlying_type_t<Enum>>(enumerator);
}

}
