/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QSize>

namespace KWin::base
{

struct output_topology {
    int current{0};
};

}

Q_DECLARE_METATYPE(KWin::base::output_topology);
