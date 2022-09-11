/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#pragma once

#include "backend.h"
#include "buffer.h"
#include "shadow.h"
#include "window.h"

#include "render/scene.h"

#include <QElapsedTimer>

namespace KWin::render::qpainter
{

template<typename Platform>
class scene : public render::scene<Platform>
{
public:
    using type = scene<Platform>;
    using abstract_type = render::scene<Platform>;

    using window_t = typename abstract_type::window_t;
    using qpainter_window_t = qpainter::window<typename window_t::ref_t, type>;

    using buffer_t = buffer<window_t>;

    using output_t = typename abstract_type::output_t;

    explicit scene(Platform& platform)
        : render::scene<Platform>(platform)
        , m_backend{platform.get_qpainter_backend(*platform.compositor)}
        , m_painter(new QPainter())
    {
        QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
    }

    int64_t paint_output(output_t* output,
                         QRegion damage,
                         std::deque<typename window_t::ref_t*> const& windows,
                         std::chrono::milliseconds presentTime) override
    {
        QElapsedTimer renderTimer;
        renderTimer.start();

        this->createStackingOrder(windows);

        auto mask = paint_type::none;
        m_backend->begin_render(*output);

        auto const needsFullRepaint = m_backend->needsFullRepaint();
        if (needsFullRepaint) {
            mask |= render::paint_type::screen_background_first;
            damage = QRect({}, kwinApp()->get_base().topology.size);
        }

        auto const geometry = output->geometry();

        auto buffer = m_backend->bufferForScreen(output);
        if (!buffer || buffer->isNull()) {
            return renderTimer.nsecsElapsed();
        }

        m_painter->begin(buffer);
        m_painter->save();
        m_painter->setWindow(geometry);

        this->repaint_output = output;
        QRegion updateRegion, validRegion;

        this->paintScreen(mask,
                          damage.intersected(geometry),
                          QRegion(),
                          &updateRegion,
                          &validRegion,
                          presentTime);
        paintCursor();

        m_painter->restore();
        m_painter->end();

        m_backend->present(output, updateRegion);

        this->clearStackingOrder();
        return renderTimer.nsecsElapsed();
    }

    void paintGenericScreen(paint_type mask, ScreenPaintData data) override
    {
        m_painter->save();
        m_painter->translate(data.xTranslation(), data.yTranslation());
        m_painter->scale(data.xScale(), data.yScale());
        render::scene<Platform>::paintGenericScreen(mask, data);
        m_painter->restore();
    }

    CompositingType compositingType() const override
    {
        return QPainterCompositing;
    }

    std::unique_ptr<render::shadow<window_t>> createShadow(window_t* window) override
    {
        return std::make_unique<shadow<window_t>>(window);
    }

    win::deco::renderer<win::deco::client_impl<typename window_t::ref_t>>*
    createDecorationRenderer(win::deco::client_impl<typename window_t::ref_t>* impl) override
    {
        return new deco_renderer(impl);
    }

    void handle_screen_geometry_change(QSize const& /*size*/) override
    {
    }

    bool animationsSupported() const override
    {
        return false;
    }

    QPainter* scenePainter() const override
    {
        return m_painter.data();
    }

    qpainter::backend<type>* backend() const
    {
        return m_backend;
    }

protected:
    void paintBackground(QRegion region) override
    {
        m_painter->setBrush(Qt::black);
        for (const QRect& rect : region) {
            m_painter->drawRect(rect);
        }
    }

    std::unique_ptr<window_t> createWindow(typename window_t::ref_t* ref_win) override
    {
        return std::make_unique<qpainter_window_t>(ref_win, *this);
    }

    void paintCursor() override
    {
        auto cursor = this->platform.compositor->software_cursor.get();
        if (!cursor->enabled) {
            return;
        }
        auto const img = cursor->image();
        if (img.isNull()) {
            return;
        }
        auto const cursorPos = this->platform.base.space->input->cursor->pos();
        auto const hotspot = cursor->hotspot();
        m_painter->drawImage(cursorPos - hotspot, img);
        cursor->mark_as_rendered();
    }

    void paintEffectQuickView(EffectQuickView* w) override
    {
        auto painter = this->platform.compositor->effects->scenePainter();
        const QImage buffer = w->bufferAsImage();
        if (buffer.isNull()) {
            return;
        }
        painter->save();
        painter->setOpacity(w->opacity());
        painter->drawImage(w->geometry(), buffer);
        painter->restore();
    }

private:
    qpainter::backend<type>* m_backend;
    QScopedPointer<QPainter> m_painter;
};

template<typename Platform>
std::unique_ptr<render::scene<Platform>> create_scene(Platform& platform)
{
    qCDebug(KWIN_CORE) << "Creating QPainter scene.";
    return std::make_unique<qpainter::scene<Platform>>(platform);
}

}
