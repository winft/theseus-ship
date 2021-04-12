/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "scene_qpainter_x11_backend.h"

#include "abstract_output.h"
#include "x11windowed_backend.h"
#include "screens.h"

namespace KWin
{
X11WindowedQPainterBackend::X11WindowedQPainterBackend(X11WindowedBackend *backend)
    : QPainterBackend()
    , m_backend(backend)
{
    connect(screens(), &Screens::changed, this, &X11WindowedQPainterBackend::createOutputs);
    createOutputs();
}

X11WindowedQPainterBackend::~X11WindowedQPainterBackend()
{
    qDeleteAll(m_outputs);
    if (m_gc) {
        xcb_free_gc(m_backend->connection(), m_gc);
    }
}

void X11WindowedQPainterBackend::createOutputs()
{
    qDeleteAll(m_outputs);
    m_outputs.clear();
    for (auto out : m_backend->enabledOutputs()) {
        Output *output = new Output;
        output->output = out;
        output->window = m_backend->windowForScreen(out);
        output->buffer = QImage(out->geometry().size() * out->scale(), QImage::Format_RGB32);
        output->buffer.fill(Qt::black);
        m_outputs << output;
    }
    m_needsFullRepaint = true;
}

X11WindowedQPainterBackend::Output* X11WindowedQPainterBackend::get_output(AbstractOutput* output)
{
    for (auto out: m_outputs) {
        if (out->output == output) {
            return out;
        }
    }
    assert(false);
    return m_outputs[0];
}

QImage *X11WindowedQPainterBackend::buffer()
{
    return bufferForScreen(0);
}

QImage *X11WindowedQPainterBackend::bufferForScreen(AbstractOutput* output)
{
    return &get_output(output)->buffer;
}

bool X11WindowedQPainterBackend::needsFullRepaint() const
{
    return m_needsFullRepaint;
}

void X11WindowedQPainterBackend::prepareRenderingFrame()
{
}

void X11WindowedQPainterBackend::present(AbstractOutput* output, const QRegion &damage)
{
    Q_UNUSED(damage)

    xcb_connection_t *c = m_backend->connection();
    const xcb_window_t window = m_backend->window();
    if (m_gc == XCB_NONE) {
        m_gc = xcb_generate_id(c);
        xcb_create_gc(c, m_gc, window, 0, nullptr);
    }

    auto const out = get_output(output);

    // TODO: only update changes?
    auto const& buffer = out->buffer;
    xcb_put_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, out->window, m_gc,
                  buffer.width(), buffer.height(),
                  0, 0, 0, 24,
                  buffer.sizeInBytes(), buffer.constBits());
}

}
