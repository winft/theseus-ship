/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xwl_window.h"

#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

qreal xwl_window::bufferScale() const
{
    return surface() ? surface()->state().scale : 1;
}

}
