/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/x11/selection_owner.h>
#include <kwin_export.h>

#include <xcb/xcb.h>

namespace KWin::base::backend::x11
{

class KWIN_EXPORT wm_selection_owner : public base::x11::selection_owner
{
public:
    wm_selection_owner(xcb_connection_t* con, int screen);

protected:
    bool
    genericReply(xcb_atom_t target_P, xcb_atom_t property_P, xcb_window_t requestor_P) override;
    void replyTargets(xcb_atom_t property_P, xcb_window_t requestor_P) override;
    void getAtoms() override;
    xcb_atom_t make_selection_atom(xcb_connection_t* con, int screen_P);

private:
    static xcb_atom_t xa_version;
    xcb_connection_t* con;
};

}
