/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"

#include "compositor.h"
#include "platform.h"

#include "input/cursor.h"
#include "input/platform.h"
#include "input/pointer_redirect.h"
#include "main.h"

namespace KWin::render
{

cursor::cursor(render::platform& platform, input::platform* input)
    : platform{platform}
    , input{input}
{
    connect(this, &cursor::changed, this, &cursor::rerender);
}

QImage cursor::image() const
{
    return input->cursor->image();
}

QPoint cursor::hotspot() const
{
    return input->cursor->hotspot();
}

void cursor::mark_as_rendered()
{
    if (enabled) {
        last_rendered_geometry = QRect(input->cursor->pos() - hotspot(), image().size());
    }
    input->cursor->mark_as_rendered();
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

    auto cursor = input::get_cursor();
    if (enable) {
        cursor->start_image_tracking();
        connect(cursor, &input::cursor::pos_changed, this, &cursor::rerender);
        connect(cursor, &input::cursor::image_changed, this, &cursor::changed);
    } else {
        cursor->stop_image_tracking();
        disconnect(cursor, &input::cursor::pos_changed, this, &cursor::rerender);
        disconnect(cursor, &input::cursor::image_changed, this, &cursor::changed);
    }
}

void cursor::rerender()
{
    auto& compositor = platform.compositor;
    compositor->addRepaint(last_rendered_geometry);
    compositor->addRepaint(QRect(input->cursor->pos() - hotspot(), image().size()));
}

}
