/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/x11/window.h"

namespace KWin::win::wayland
{

class KWIN_EXPORT xwl_window : public x11::window
{
    Q_OBJECT
public:
    xwl_window(xcb_window_t xcb_win, win::space& space);

    qreal bufferScale() const override;
    void add_scene_window_addon() override;
};

}

Q_DECLARE_METATYPE(KWin::win::wayland::xwl_window*)
