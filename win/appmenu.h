/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <string>

namespace KWin::win
{

struct appmenu_address {
    appmenu_address() = default;
    appmenu_address(std::string name, std::string path)
        : name{name}
        , path{path}
    {
    }

    bool operator==(appmenu_address const& other) const
    {
        return name == other.name && path == other.path;
    }
    bool empty() const
    {
        return name.empty() && path.empty();
    }

    std::string name;
    std::string path;
};

struct appmenu {
    bool active{false};
    appmenu_address address;
};

}
