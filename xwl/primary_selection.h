/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_data.h"

#include <Wrapland/Server/primary_selection.h>

namespace KWin::xwl
{
class primary_selection_source_ext;

class primary_selection
{
public:
    selection_data<Wrapland::Server::primary_selection_source, primary_selection_source_ext> data;

    primary_selection(x11_data const& x11);

    Wrapland::Server::primary_selection_source* get_current_source() const;
    void set_selection(Wrapland::Server::primary_selection_source* source) const;

private:
    Q_DISABLE_COPY(primary_selection)
};

}
