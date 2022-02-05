/*
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/atoms.h"

#include <string>
#include <xcb/xcb.h>

namespace KWin::xwl
{

inline xcb_atom_t mime_type_to_atom_literal(std::string const& mime_type)
{
    return Xcb::Atom(mime_type.c_str(), false, kwinApp()->x11Connection());
}

inline xcb_atom_t mime_type_to_atom(std::string const& mime_type, base::x11::atoms const& atoms)
{
    if (mime_type == "text/plain;charset=utf-8") {
        return atoms.utf8_string;
    }
    if (mime_type == "text/plain") {
        return atoms.text;
    }
    if (mime_type == "text/x-uri") {
        return atoms.uri_list;
    }
    return mime_type_to_atom_literal(mime_type);
}

inline std::string atom_name(xcb_atom_t atom)
{
    auto xcb_con = kwinApp()->x11Connection();
    auto name_cookie = xcb_get_atom_name(xcb_con, atom);
    auto name_reply = xcb_get_atom_name_reply(xcb_con, name_cookie, nullptr);
    if (!name_reply) {
        return std::string();
    }

    auto const length = xcb_get_atom_name_name_length(name_reply);
    auto const name = std::string(xcb_get_atom_name_name(name_reply), length);

    free(name_reply);
    return name;
}

inline std::vector<std::string> atom_to_mime_types(xcb_atom_t atom, base::x11::atoms const& atoms)
{
    std::vector<std::string> mime_types;

    if (atom == atoms.utf8_string) {
        mime_types.emplace_back("text/plain;charset=utf-8");
    } else if (atom == atoms.text) {
        mime_types.emplace_back("text/plain");
    } else if (atom == atoms.uri_list || atom == atoms.netscape_url || atom == atoms.moz_url) {
        // We identify netscape and moz format as less detailed formats text/uri-list,
        // text/x-uri and accept the information loss.
        mime_types.emplace_back("text/uri-list");
        mime_types.emplace_back("text/x-uri");
    } else {
        mime_types.emplace_back(atom_name(atom));
    }
    return mime_types;
}

}
