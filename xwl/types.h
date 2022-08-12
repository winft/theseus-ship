/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xwayland_interface.h"

#include "base/x11/atoms.h"

#include <string>
#include <vector>
#include <xcb/xcb.h>

namespace KWin
{

namespace win
{
class space;
}

namespace xwl
{

struct x11_runtime {
    xcb_connection_t* connection{nullptr};
    xcb_screen_t* screen{nullptr};
    base::x11::atoms* atoms{nullptr};
};

struct runtime {
    win::space* space{nullptr};
    x11_runtime x11{};
};

struct mime_atom {
    mime_atom(std::string const& id, xcb_atom_t atom)
        : id{id}
        , atom{atom}
    {
    }
    bool operator==(mime_atom const& rhs) const
    {
        return id == rhs.id && atom == rhs.atom;
    }
    bool operator!=(mime_atom const& rhs) const
    {
        return !(*this == rhs);
    }

    std::string id;
    xcb_atom_t atom{XCB_ATOM_NONE};
};

using mime_atoms = std::vector<mime_atom>;

}
}
