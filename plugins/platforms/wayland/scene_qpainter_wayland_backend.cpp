/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>
Copyright 2013, 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "scene_qpainter_wayland_backend.h"
#include "wayland_backend.h"
#include "wayland_output.h"

#include "composite.h"
#include "logging.h"

#include <Wrapland/Client/buffer.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

namespace KWin
{
namespace Wayland
{

WaylandQPainterOutput::WaylandQPainterOutput(WaylandOutput *output, QObject *parent)
    : QObject(parent)
    , m_waylandOutput(output)
{
}

WaylandQPainterOutput::~WaylandQPainterOutput()
{
    if (auto buffer = m_buffer.lock()) {
        buffer->setUsed(false);
    }
}

bool WaylandQPainterOutput::init(Wrapland::Client::ShmPool *pool)
{
    m_pool = pool;
    m_backBuffer = QImage(QSize(), QImage::Format_RGB32);

    connect(pool, &Wrapland::Client::ShmPool::poolResized, this, &WaylandQPainterOutput::remapBuffer);
    connect(m_waylandOutput, &WaylandOutput::sizeChanged, this, &WaylandQPainterOutput::updateSize);

    return true;
}

void WaylandQPainterOutput::remapBuffer()
{
    auto buffer = m_buffer.lock();
    if (!buffer) {
        return;
    }
    if (!buffer->isUsed()){
        return;
    }
    const QSize size = m_backBuffer.size();
    m_backBuffer = QImage(buffer->address(), size.width(), size.height(), QImage::Format_RGB32);
    qCDebug(KWIN_WAYLAND_BACKEND) << "Remapped back buffer of surface" << m_waylandOutput->surface();
}

void WaylandQPainterOutput::updateSize(const QSize &size)
{
    Q_UNUSED(size)
    if (auto buffer = m_buffer.lock()) {
        buffer->setUsed(false);
        m_buffer.reset();
    }
}

void WaylandQPainterOutput::present(const QRegion &damage)
{
    auto s = m_waylandOutput->surface();
    s->attachBuffer(m_buffer);
    s->damage(damage);
    s->commit();
    m_waylandOutput->present();
}

void WaylandQPainterOutput::prepareRenderingFrame()
{
    if (auto buffer = m_buffer.lock()) {
        if (buffer->isReleased()) {
            // we can re-use this buffer
            buffer->setReleased(false);
            return;
        } else {
            // buffer is still in use, get a new one
            buffer->setUsed(false);
        }
    }
    m_buffer.reset();

    const QSize size(m_waylandOutput->geometry().size());

    m_buffer = m_pool->getBuffer(size, size.width() * 4);
    auto buffer = m_buffer.lock();
    if (!buffer) {
        qCDebug(KWIN_WAYLAND_BACKEND) << "Did not get a new Buffer from Shm Pool";
        m_backBuffer = QImage();
        return;
    }

    buffer->setUsed(true);

    m_backBuffer = QImage(buffer->address(), size.width(), size.height(), QImage::Format_RGB32);
    m_backBuffer.fill(Qt::transparent);
//    qCDebug(KWIN_WAYLAND_BACKEND) << "Created a new back buffer for output surface" << m_waylandOutput->surface();
}

WaylandQPainterBackend::WaylandQPainterBackend(Wayland::WaylandBackend *b)
    : QPainterBackend()
    , m_backend(b)
    , m_needsFullRepaint(true)
{

    const auto waylandOutputs = m_backend->waylandOutputs();
    for (auto *output: waylandOutputs) {
        createOutput(output);
    }
    connect(m_backend, &WaylandBackend::output_added, this, [this](auto output) {
        createOutput(static_cast<WaylandOutput*>(output));
    });
    connect(m_backend, &WaylandBackend::output_removed, this,
        [this] (auto output) {
            auto waylandOutput = static_cast<WaylandOutput*>(output);
            auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
                [waylandOutput] (WaylandQPainterOutput *output) {
                    return output->m_waylandOutput == waylandOutput;
                }
            );
            if (it == m_outputs.end()) {
                return;
            }
            delete *it;
            m_outputs.erase(it);
        }
    );
}

WaylandQPainterBackend::~WaylandQPainterBackend()
{
}

void WaylandQPainterBackend::createOutput(WaylandOutput *waylandOutput)
{
    auto *output = new WaylandQPainterOutput(waylandOutput, this);
    output->init(m_backend->shmPool());
    m_outputs << output;
}

WaylandQPainterOutput* WaylandQPainterBackend::get_output(AbstractOutput* output)
{
    for (auto& out: m_outputs) {
        if (out->m_waylandOutput == output) {
            return out;
        }
    }
    assert(false);
    return m_outputs[0];
}

void WaylandQPainterBackend::present(AbstractOutput* output, const QRegion &damage)
{
    m_needsFullRepaint = false;

    get_output(output)->present(damage);
}

QImage *WaylandQPainterBackend::buffer()
{
    return bufferForScreen(0);
}

QImage *WaylandQPainterBackend::bufferForScreen(AbstractOutput* output)
{
    auto out = get_output(output);
    return &out->m_backBuffer;
}

void WaylandQPainterBackend::prepareRenderingFrame()
{
    for (auto *output : m_outputs) {
        output->prepareRenderingFrame();
    }
    m_needsFullRepaint = true;
}

bool WaylandQPainterBackend::needsFullRepaint() const
{
    return m_needsFullRepaint;
}

}
}
