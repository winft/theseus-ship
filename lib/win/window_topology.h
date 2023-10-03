/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"
#include "virtual_desktops.h"

namespace KWin::win
{

template<typename Output>
struct window_topology {
    win::layer layer{layer::unknown};
    Output const* central_output{nullptr};
    QVector<subspace*> subspaces;
};

}
