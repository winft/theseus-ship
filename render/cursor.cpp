/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"

#include "compositor.h"
#include <input/cursor.h>
#include <input/platform.h>
#include <input/pointer_redirect.h>
#include <main.h>
#include <platform.h>

namespace KWin::render
{

cursor::cursor(input::platform* input)
    : QObject()
    , input{input}
{
}

QImage cursor::image() const
{
    return input->redirect->pointer()->cursorImage();
}

QPoint cursor::hotspot() const
{
    return input->redirect->pointer()->cursorHotSpot();
}

void cursor::mark_as_rendered()
{
    if (enabled) {
        last_rendered_geometry = QRect(input->cursor->pos() - hotspot(), image().size());
    }
    if (auto pointer = input->redirect->pointer()) {
        pointer->markCursorAsRendered();
    }
}

void cursor::set_enabled(bool enable)
{
    if (qEnvironmentVariableIsSet("KWIN_FORCE_SW_CURSOR")) {
        enable = true;
    }
    if (enabled == enable) {
        return;
    }

    enabled = enable;

    if (enable) {
        connect(input->cursor.get(), &input::cursor::pos_changed, this, &cursor::rerender);
        connect(kwinApp()->platform, &Platform::cursorChanged, this, &cursor::rerender);
    } else {
        disconnect(input->cursor.get(), &input::cursor::pos_changed, this, &cursor::rerender);
        disconnect(kwinApp()->platform, &Platform::cursorChanged, this, &cursor::rerender);
    }
}

void cursor::rerender()
{
    auto compositor = compositor::self();
    if (!compositor) {
        return;
    }
    compositor->addRepaint(last_rendered_geometry);
    compositor->addRepaint(QRect(input->cursor->pos() - hotspot(), image().size()));
}

}
