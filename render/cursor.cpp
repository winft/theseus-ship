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

cursor_qobject::cursor_qobject() = default;

cursor::cursor(render::platform& platform, input::platform* input)
    : qobject{std::make_unique<cursor_qobject>()}
    , platform{platform}
    , input{input}
{
    QObject::connect(
        qobject.get(), &cursor_qobject::changed, qobject.get(), [this] { rerender(); });
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
    auto cursor = input->cursor.get();

    if (enable) {
        cursor->start_image_tracking();
        notifiers.pos = QObject::connect(
            cursor, &input::cursor::pos_changed, qobject.get(), [this] { rerender(); });
        notifiers.image = QObject::connect(
            cursor, &input::cursor::image_changed, qobject.get(), &cursor_qobject::changed);
    } else {
        cursor->stop_image_tracking();
        QObject::disconnect(notifiers.pos);
        QObject::disconnect(notifiers.image);
    }
}

void cursor::rerender()
{
    auto& compositor = platform.compositor;
    compositor->addRepaint(last_rendered_geometry);
    compositor->addRepaint(QRect(input->cursor->pos() - hotspot(), image().size()));
}

}
