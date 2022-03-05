/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: MIT
*/
#pragma once

#include <QPoint>
#include <climits>

namespace KWin::geo
{

// TODO(romangg): The goal is to have no Qt types at all in the utils directory. Replace with our
//                own "point" implementation.
QPoint const invalid_point(INT_MIN, INT_MIN);

}
