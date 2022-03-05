/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "backend.h"

#include "base/output.h"
#include "base/platform.h"
#include "main.h"
#include "win/space.h"

#include <kwineffects/effects_handler.h>

#include <epoxy/gl.h>

namespace KWin::render::gl
{

backend::backend()
    : m_directRendering(false)
    , m_haveBufferAge(false)
{
}

backend::~backend()
{
}

void backend::idle()
{
    if (hasPendingFlush()) {
        effects->makeOpenGLContextCurrent();
        present();
    }
}

void backend::addToDamageHistory(const QRegion& region)
{
    if (m_damageHistory.count() > 10)
        m_damageHistory.removeLast();

    m_damageHistory.prepend(region);
}

QRegion backend::accumulatedDamageHistory(int bufferAge) const
{
    QRegion region;

    // Note: An age of zero means the buffer contents are undefined
    if (bufferAge > 0 && bufferAge <= m_damageHistory.count()) {
        for (int i = 0; i < bufferAge - 1; i++)
            region |= m_damageHistory[i];
    } else {
        auto const& size = kwinApp()->get_base().topology.size;
        region = QRegion(0, 0, size.width(), size.height());
    }

    return region;
}

QRegion backend::prepareRenderingForScreen(base::output* output)
{
    // fallback to repaint complete screen
    return output->geometry();
}

void backend::endRenderingFrameForScreen(base::output* output,
                                         const QRegion& damage,
                                         const QRegion& damagedRegion)
{
    Q_UNUSED(output)
    Q_UNUSED(damage)
    Q_UNUSED(damagedRegion)
}

void backend::copyPixels(const QRegion& region)
{
    auto const height = kwinApp()->get_base().topology.size.height();
    for (const QRect& r : region) {
        const int x0 = r.x();
        const int y0 = height - r.y() - r.height();
        const int x1 = r.x() + r.width();
        const int y1 = height - r.y();

        glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
}

}
