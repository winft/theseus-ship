/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

namespace KWin::input
{

touch::touch(platform* plat, QObject* parent)
    : QObject(parent)
    , plat{plat}
{
}

touch::~touch()
{
}

}
