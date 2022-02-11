/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "options.h"

#include <QVector>
#include <algorithm>

namespace KWin::render
{

template<typename Platform>
QVector<CompositingType> get_supported_render_types(Platform const& platform)
{
    auto comps = platform.supportedCompositors();
    auto const user_cfg_it
        = std::find(comps.begin(), comps.end(), kwinApp()->options->compositingMode());

    if (user_cfg_it != comps.end()) {
        comps.erase(user_cfg_it);
        comps.prepend(kwinApp()->options->compositingMode());
    } else {
        qWarning() << "Configured compositor not supported by Platform. Falling back to defaults";
    }

    return comps;
}

}
