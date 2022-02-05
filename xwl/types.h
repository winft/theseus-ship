/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xwayland_interface.h"

#include "atoms.h"

#include <string>
#include <vector>
#include <xcb/xcb.h>

namespace KWin::xwl
{

struct x11_data {
    xcb_connection_t* connection{nullptr};
    xcb_screen_t* screen{nullptr};
    Atoms* atoms{nullptr};
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
