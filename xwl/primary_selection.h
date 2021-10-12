/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection.h"

#include <Wrapland/Server/primary_selection.h>

#include <functional>

namespace KWin::Xwl
{

class primary_selection
{
public:
    selection_data<Wrapland::Server::primary_selection_source, primary_selection_source_ext> data;

    primary_selection(xcb_atom_t atom, x11_data const& x11);

    Wrapland::Server::primary_selection_source* get_current_source() const;
    std::function<void(Wrapland::Server::primary_selection_source*)> get_selection_setter() const;

private:
    Q_DISABLE_COPY(primary_selection)
};

}
