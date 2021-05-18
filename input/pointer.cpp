/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

namespace KWin::input
{

pointer::pointer(platform* plat, QObject* parent)
    : QObject(parent)
    , plat{plat}
{
}

pointer::~pointer()
{
}

}
