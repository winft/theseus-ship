/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
        QQuickWindow::setSceneGraphBackend("software");
    }

    int64_t paint_output(output_t* output,
                         QRegion damage,
                         std::deque<typename window_t::ref_t> const& ref_wins,
                         std::chrono::milliseconds presentTime) override
    {
        QElapsedTimer renderTimer;
        renderTimer.start();

        this->createStackingOrder(ref_wins);

        auto mask = paint_type::none;
        m_backend->begin_render(*output);

        auto const needsFullRepaint = m_backend->needsFullRepaint();
        if (needsFullRepaint) {
            mask |= render::paint_type::screen_background_first;
            damage = QRect({}, this->platform.base.topology.size);
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

    std::unique_ptr<render::shadow<window_t>> createShadow(window_t* win) override
    {
        return std::make_unique<shadow<window_t>>(win);
    }

    std::unique_ptr<win::deco::render_injector>
    create_deco(win::deco::render_window window) override
    {
        return std::make_unique<deco_renderer>(std::move(window));
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

    std::unique_ptr<window_t> createWindow(typename window_t::ref_t ref_win) override
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

    void paintEffectQuickView(EffectQuickView* view) override
    {
        auto painter = this->platform.compositor->effects->scenePainter();
        const QImage buffer = view->bufferAsImage();
        if (buffer.isNull()) {
            return;
        }
        painter->save();
        painter->setOpacity(view->opacity());
        painter->drawImage(view->geometry(), buffer);
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
