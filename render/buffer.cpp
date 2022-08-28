/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "buffer.h"

#include "base/logging.h"
#include "render/window.h"
#include "toplevel.h"
#include "win/geo.h"

namespace KWin::render
{

buffer::buffer(render::window* window)
    : window{window}
    , m_discarded(false)
{
}

buffer::~buffer()
{
}

void buffer::create()
{
    if (isValid() || window->ref_win->remnant) {
        return;
    }

    updateBuffer();

    // TODO(romangg): Do we need to exclude the internal image case?
    if (win_integration->valid()) {
        window->unreference_previous_buffer();
    }
}

bool buffer::isValid() const
{
    assert(win_integration);
    return win_integration->valid();
}

void buffer::updateBuffer()
{
    assert(win_integration);
    win_integration->update();
}

bool buffer::isDiscarded() const
{
    return m_discarded;
}

void buffer::markAsDiscarded()
{
    m_discarded = true;
    window->reference_previous_buffer();
}

}
