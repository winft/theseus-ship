/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Fredrik Höglund <fredrik@kde.org>
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
#include "scene.h"

#include "backend.h"
#include "deco_renderer.h"
#include "effect_frame.h"
#include "shadow.h"
#include "window.h"

#include "render/effects.h"
#include "render/platform.h"
#include "render/shadow.h"
#include "render/x11/compositor.h"
#include "toplevel.h"
#include "win/geo.h"
#include "win/scene.h"
#include "win/x11/window.h"

#include <kwineffects/paint_clipper.h>

#include <cassert>

namespace KWin::render::xrender
{

ScreenPaintData scene::screen_paint;

scene::scene(xrender::backend* backend, render::compositor& compositor)
    : render::scene(compositor)
    , m_backend(backend)
{
}

scene::~scene()
{
    window::cleanup();
    effect_frame::cleanup();
}

bool scene::initFailed() const
{
    return false;
}

// the entry point for painting
int64_t scene::paint(QRegion damage,
                     std::deque<Toplevel*> const& toplevels,
                     std::chrono::milliseconds presentTime)
{
    QElapsedTimer renderTimer;
    renderTimer.start();

    createStackingOrder(toplevels);

    auto mask = paint_type::none;
    QRegion updateRegion, validRegion;
    paintScreen(mask, damage, QRegion(), &updateRegion, &validRegion, presentTime);

    m_backend->showOverlay();

    m_backend->present(mask, updateRegion);
    // do cleanup
    clearStackingOrder();

    return renderTimer.nsecsElapsed();
}

void scene::paintGenericScreen(paint_type mask, ScreenPaintData data)
{
    screen_paint = data; // save, transformations will be done when painting windows
    render::scene::paintGenericScreen(mask, data);
}

void scene::paintDesktop(int desktop, paint_type mask, const QRegion& region, ScreenPaintData& data)
{
    PaintClipper::push(region);
    render::scene::paintDesktop(desktop, mask, region, data);
    PaintClipper::pop(region);
}

// fill the screen background
void scene::paintBackground(QRegion region)
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

std::unique_ptr<render::window> scene::createWindow(Toplevel* toplevel)
{
    return std::make_unique<window>(toplevel, this);
}

render::effect_frame* scene::createEffectFrame(effect_frame_impl* frame)
{
    return new effect_frame(frame);
}

std::unique_ptr<render::shadow> scene::createShadow(Toplevel* toplevel)
{
    return std::make_unique<shadow>(toplevel);
}

win::deco::renderer* scene::createDecorationRenderer(win::deco::client_impl* client)
{
    return new deco_renderer(client);
}

render::scene* create_scene(x11::compositor& compositor)
{
    QScopedPointer<xrender::backend> backend;
    backend.reset(new xrender::backend(compositor));
    if (backend->isFailed()) {
        return nullptr;
    }
    return new scene(backend.take(), compositor);
}

void scene::paintCursor()
{
}

void scene::paintEffectQuickView(KWin::EffectQuickView* w)
{
    const QImage buffer = w->bufferAsImage();
    if (buffer.isNull()) {
        return;
    }
    XRenderPicture picture(buffer);
    xcb_render_composite(connection(),
                         XCB_RENDER_PICT_OP_OVER,
                         picture,
                         XCB_RENDER_PICTURE_NONE,
                         effects->xrenderBufferPicture(),
                         0,
                         0,
                         0,
                         0,
                         w->geometry().x(),
                         w->geometry().y(),
                         w->geometry().width(),
                         w->geometry().height());
}

}
