/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/setup_window.h>

namespace KWin::render::wayland
{

template<typename Handler, typename Win>
void effect_setup_window(Handler& handler, Win& window)
{
    if (window.render_data.ready_for_painting) {
        handler.slotXdgShellClientShown(window);
        return;
    }

    QObject::connect(window.qobject.get(),
                     &win::window_qobject::windowShown,
                     &handler,
                     [&handler, &window] { handler.slotXdgShellClientShown(window); });
}

}
