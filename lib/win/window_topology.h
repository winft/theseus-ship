/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/subspace.h>
#include <win/types.h>

namespace KWin::win
{

template<typename Output>
struct window_topology {
    win::layer layer{layer::unknown};
    Output const* central_output{nullptr};
    QVector<subspace*> subspaces;
};

}
