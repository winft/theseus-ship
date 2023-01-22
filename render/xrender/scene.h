/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

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
#include "deco_renderer.h"
#include "shadow.h"
#include "window.h"

#include "base/logging.h"
#include "render/scene.h"

#include <QElapsedTimer>
#include <kwinxrender/utils.h>

namespace KWin::render::xrender
{

template<typename Platform>
class scene : public render::scene<Platform>
{
public:
    using type = scene<Platform>;
    using abstract_type = render::scene<Platform>;
    using compositor_t = typename Platform::compositor_t;

    using window_t = typename abstract_type::window_t;
    using xrender_window_t = xrender::window<typename window_t::ref_t, type>;
    using buffer_t = buffer<window_t>;

    using space_t = typename abstract_type::space_t;

    explicit scene(Platform& platform)
        : render::scene<Platform>(platform)
        , m_backend{std::make_unique<xrender::backend<type>>(*this)}
    {
    }

    ~scene() override
    {
        delete temp_picture;
        temp_picture = nullptr;
        delete fade_alpha_picture;
        fade_alpha_picture = nullptr;
    }

    CompositingType compositingType() const override
    {
        return XRenderCompositing;
    }
    int64_t paint(QRegion damage,
                  std::deque<typename space_t::window_t> const& windows,
                  std::chrono::milliseconds presentTime) override
    {
        QElapsedTimer renderTimer;
        renderTimer.start();

        this->createStackingOrder(windows);

        auto mask = paint_type::none;
        QRegion updateRegion, validRegion;
        this->paintScreen(mask, damage, QRegion(), &updateRegion, &validRegion, presentTime);

        m_backend->showOverlay();

        m_backend->present(mask, updateRegion);
        // do cleanup
        this->clearStackingOrder();

        return renderTimer.nsecsElapsed();
    }

    std::unique_ptr<render::shadow<window_t>> createShadow(window_t* win) override
    {
        return std::make_unique<xrender::shadow<window_t>>(win);
    }

    void handle_screen_geometry_change(QSize const& size) override
    {
        m_backend->screenGeometryChanged(size);
    }

    xcb_render_picture_t xrenderBufferPicture() const override
    {
        return m_backend->buffer();
    }

    std::unique_ptr<win::deco::render_injector>
    create_deco(win::deco::render_window window) override
    {
        return std::make_unique<deco_renderer>(this->platform.base.x11_data, std::move(window));
    }

    bool animationsSupported() const override
    {
        return true;
    }

    ScreenPaintData screen_paint;
    QRect temp_visible_rect;
    XRenderPicture* temp_picture{nullptr};
    XRenderPicture* fade_alpha_picture{nullptr};

protected:
    std::unique_ptr<window_t> createWindow(typename space_t::window_t ref_win) override
    {
        return std::make_unique<xrender_window_t>(ref_win, *this);
    }

    void paintBackground(QRegion region) override
    {
        xcb_render_color_t col = {0, 0, 0, 0xffff}; // black
        const QVector<xcb_rectangle_t>& rects = base::x11::xcb::qt_region_to_rects(region);
        xcb_render_fill_rectangles(connection(),
                                   XCB_RENDER_PICT_OP_SRC,
                                   xrenderBufferPicture(),
                                   col,
                                   rects.count(),
                                   rects.data());
    }

    void paintGenericScreen(paint_type mask, ScreenPaintData data) override
    {
        screen_paint = data; // save, transformations will be done when painting windows
        abstract_type::paintGenericScreen(mask, data);
    }

    void paintDesktop(int desktop,
                      paint_type mask,
                      const QRegion& region,
                      ScreenPaintData& data) override
    {
        PaintClipper::push(region);
        abstract_type::paintDesktop(desktop, mask, region, data);
        PaintClipper::pop(region);
    }

    void paintCursor() override
    {
    }

    void paintEffectQuickView(EffectQuickView* view) override
    {
        auto const buffer = view->bufferAsImage();
        if (buffer.isNull()) {
            return;
        }
        XRenderPicture picture(buffer);
        xcb_render_composite(connection(),
                             XCB_RENDER_PICT_OP_OVER,
                             picture,
                             XCB_RENDER_PICTURE_NONE,
                             this->platform.compositor->effects->xrenderBufferPicture(),
                             0,
                             0,
                             0,
                             0,
                             view->geometry().x(),
                             view->geometry().y(),
                             view->geometry().width(),
                             view->geometry().height());
    }

private:
    std::unique_ptr<xrender::backend<type>> m_backend;
};

template<typename Platform>
std::unique_ptr<render::scene<Platform>> create_scene(Platform& platform)
{
    qCDebug(KWIN_CORE) << "Creating XRender scene.";
    return std::make_unique<xrender::scene<Platform>>(platform);
}

}
