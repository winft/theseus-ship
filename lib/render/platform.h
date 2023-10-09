/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "outline.h"

namespace KWin::render
{

class platform
{
public:
    // TODO(romangg): For unknown reason this using declaration can't be set in the child classes.
    //                Maybe because there is a Q_OBJECT macro in render::outline?
    using outline_t = render::outline;

    virtual ~platform() = default;

};

}
