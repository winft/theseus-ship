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

/**
 The base class for compositing, implementing shared functionality
 between the OpenGL and XRender backends.

 Design:

 When compositing is turned on, XComposite extension is used to redirect
 drawing of windows to pixmaps and XDamage extension is used to get informed
 about damage (changes) to window contents. This code is mostly in composite.cpp .

 Compositor::performCompositing() starts one painting pass. Painting is done
 by painting the screen, which in turn paints every window. Painting can be affected
 using effects, which are chained. E.g. painting a screen means that actually
 paintScreen() of the first effect is called, which possibly does modifications
 and calls next effect's paintScreen() and so on, until scene::finalPaintScreen()
 is called.

 There are 3 phases of every paint (not necessarily done together):
 The pre-paint phase, the paint phase and the post-paint phase.

 The pre-paint phase is used to find out about how the painting will be actually
 done (i.e. what the effects will do). For example when only a part of the screen
 needs to be updated and no effect will do any transformation it is possible to use
 an optimized paint function. How the painting will be done is controlled
 by the mask argument, see PAINT_WINDOW_* and PAINT_SCREEN_* flags in scene.h .
 For example an effect that decides to paint a normal windows as translucent
 will need to modify the mask in its prePaintWindow() to include
 the paint_type::window_translucent flag. The paintWindow() function will then get
 the mask with this flag turned on and will also paint using transparency.

 The paint pass does the actual painting, based on the information collected
 using the pre-paint pass. After running through the effects' paintScreen()
 either paintGenericScreen() or optimized paintSimpleScreen() are called.
 Those call paintWindow() on windows (not necessarily all), possibly using
 clipping to optimize performance and calling paintWindow() first with only
 paint_type::window_opaque to paint the opaque parts and then later
 with paint_type::window_translucent to paint the transparent parts. Function
 paintWindow() again goes through effects' paintWindow() until
 finalPaintWindow() is called, which calls the window's performPaint() to
 do the actual painting.

 The post-paint can be used for cleanups and is also used for scheduling
 repaints during the next painting pass for animations. Effects wanting to
 repaint certain parts can manually damage them during post-paint and repaint
 of these parts will be done during the next paint pass.
*/

#include "scene.h"

#include "compositor.h"
#include "effect/window_impl.h"
#include "effects.h"
#include "shadow.h"
#include "singleton_interface.h"
#include "thumbnail_item.h"
#include "window.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "main.h"
#include "win/geo.h"
#include "win/scene.h"
#include "win/space.h"

#include <kwineffects/paint_clipper.h>

#include <QQuickWindow>
#include <QVector2D>

namespace KWin::render
{

//****************************************
// scene
//****************************************

scene::scene(render::compositor& compositor)
    : compositor{compositor}
{
    singleton_interface::supports_surfaceless_context
        = [this] { return supportsSurfacelessContext(); };

    QObject::connect(compositor.space->qobject.get(),
                     &win::space_qobject::remnant_created,
                     this,
                     [this](auto win_id) {
                         auto remnant = this->compositor.space->windows_map.at(win_id);
                         init_remnant(*remnant);
                     });
}

scene::~scene()
{
    singleton_interface::supports_surfaceless_context = {};
}

int64_t scene::paint(QRegion /*damage*/,
                     std::deque<Toplevel*> const& /*windows*/,
                     std::chrono::milliseconds /*presentTime*/)
{
    assert(false);
    return 0;
}

int64_t scene::paint_output(base::output* /*output*/,
                            QRegion /*damage*/,
                            std::deque<Toplevel*> const& /*windows*/,
                            std::chrono::milliseconds /*presentTime*/)
{
    assert(false);
    return 0;
}

// returns mask and possibly modified region
void scene::paintScreen(paint_type& mask,
                        const QRegion& damage,
                        const QRegion& repaint,
                        QRegion* updateRegion,
                        QRegion* validRegion,
                        std::chrono::milliseconds presentTime,
                        const QMatrix4x4& projection)
{
    auto const& space_size = kwinApp()->get_base().topology.size;
    const QRegion displayRegion(0, 0, space_size.width(), space_size.height());
    mask = (damage == displayRegion) ? paint_type::none : paint_type::screen_region;

    if (Q_UNLIKELY(presentTime < m_expectedPresentTimestamp)) {
        qCDebug(KWIN_CORE,
                "Provided presentation timestamp is invalid: %ld (current: %ld)",
                presentTime.count(),
                m_expectedPresentTimestamp.count());
    } else {
        m_expectedPresentTimestamp = presentTime;
    }

    // preparation step
    compositor.effects->startPaint();

    QRegion region = damage;

    ScreenPrePaintData pdata;
    pdata.mask = static_cast<int>(mask);
    pdata.paint = region;

    compositor.effects->prePaintScreen(pdata, m_expectedPresentTimestamp);

    mask = static_cast<paint_type>(pdata.mask);
    region = pdata.paint;

    if (flags(mask
              & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))) {
        // Region painting is not possible with transformations,
        // because screen damage doesn't match transformed positions.
        mask &= ~paint_type::screen_region;
        region = infiniteRegion();
    } else if (flags(mask & paint_type::screen_region)) {
        // make sure not to go outside visible screen
        region &= displayRegion;
    } else {
        // whole screen, not transformed, force region to be full
        region = displayRegion;
    }

    painted_region = region;
    repaint_region = repaint;

    if (flags(mask & paint_type::screen_background_first)) {
        paintBackground(region);
    }

    ScreenPaintData data(projection,
                         repaint_output ? compositor.effects->findScreen(repaint_output->name())
                                        : nullptr);
    compositor.effects->paintScreen(static_cast<int>(mask), region, data);

    for (auto const& w : stacking_order) {
        compositor.effects->postPaintWindow(w->effect.get());
    }

    compositor.effects->postPaintScreen();

    // make sure not to go outside of the screen area
    *updateRegion = damaged_region;
    *validRegion = (region | painted_region) & displayRegion;

    repaint_region = QRegion();
    damaged_region = QRegion();

    // make sure all clipping is restored
    Q_ASSERT(!PaintClipper::clip());
}

// Painting pass is optimized away.
void scene::idle()
{
}

// the function that'll be eventually called by paintScreen() above
void scene::finalPaintScreen(paint_type mask, QRegion region, ScreenPaintData& data)
{
    if (flags(mask
              & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))) {
        paintGenericScreen(mask, data);
    } else {
        paintSimpleScreen(mask, region);
    }
}

// The generic painting code that can handle even transformations.
// It simply paints bottom-to-top.
void scene::paintGenericScreen(paint_type orig_mask, ScreenPaintData)
{
    if (!(orig_mask & paint_type::screen_background_first)) {
        paintBackground(infiniteRegion());
    }
    QVector<Phase2Data> phase2;
    phase2.reserve(stacking_order.size());
    for (auto const& w : stacking_order) {
        // bottom to top
        auto topw = w->get_window();

        // Reset the repaint_region.
        // This has to be done here because many effects schedule a repaint for
        // the next frame within Effects::prePaintWindow.
        topw->resetRepaints(repaint_output);

        WindowPrePaintData data;
        data.mask = static_cast<int>(
            orig_mask
            | (w->isOpaque() ? paint_type::window_opaque : paint_type::window_translucent));
        w->resetPaintingEnabled();
        data.paint = infiniteRegion(); // no clipping, so doesn't really matter
        data.clip = QRegion();
        data.quads = w->buildQuads();

        // preparation step
        compositor.effects->prePaintWindow(w->effect.get(), data, m_expectedPresentTimestamp);
#if !defined(QT_NO_DEBUG)
        if (data.quads.isTransformed()) {
            qFatal("Pre-paint calls are not allowed to transform quads!");
        }
#endif
        if (!w->isPaintingEnabled()) {
            continue;
        }
        phase2.append(
            {w, infiniteRegion(), data.clip, static_cast<paint_type>(data.mask), data.quads});
    }

    for (auto const& d : phase2) {
        paintWindow(d.window, d.mask, d.region, d.quads);
    }

    auto const& space_size = kwinApp()->get_base().topology.size;
    damaged_region = QRegion(0, 0, space_size.width(), space_size.height());
}

// The optimized case without any transformations at all.
// It can paint only the requested region and can use clipping
// to reduce painting and improve performance.
void scene::paintSimpleScreen(paint_type orig_mask, QRegion region)
{
    Q_ASSERT(
        (orig_mask & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))
        == paint_type::none);
    QVector<Phase2Data> phase2data;
    phase2data.reserve(stacking_order.size());

    QRegion dirtyArea = region;
    bool opaqueFullscreen = false;

    // Traverse the scene windows from bottom to top.
    for (auto&& window : stacking_order) {
        auto toplevel = window->get_window();
        WindowPrePaintData data;
        data.mask = static_cast<int>(
            orig_mask
            | (window->isOpaque() ? paint_type::window_opaque : paint_type::window_translucent));
        window->resetPaintingEnabled();
        data.paint = region;
        data.paint |= toplevel->repaints();

        // Reset the repaint_region.
        // This has to be done here because many effects schedule a repaint for
        // the next frame within Effects::prePaintWindow.
        toplevel->resetRepaints(repaint_output);

        opaqueFullscreen = false;

        // TODO: do we care about unmanged windows here (maybe input windows?)
        if (window->isOpaque()) {
            if (toplevel->control) {
                opaqueFullscreen = toplevel->control->fullscreen;
            }
            data.clip |= win::content_render_region(toplevel).translated(toplevel->pos()
                                                                         + window->bufferOffset());
        } else if (toplevel->hasAlpha() && toplevel->opacity() == 1.0) {
            auto const clientShape = win::content_render_region(toplevel).translated(
                win::frame_to_render_pos(toplevel, toplevel->pos()));
            auto const opaqueShape = toplevel->opaque_region.translated(
                win::frame_to_client_pos(toplevel, toplevel->pos()) - toplevel->pos());
            data.clip = clientShape & opaqueShape;
            if (clientShape == opaqueShape) {
                data.mask = static_cast<int>(orig_mask | paint_type::window_opaque);
            }
        } else {
            data.clip = QRegion();
        }

        // Clip out decoration without alpha when window has not set additional opacity by us.
        // The decoration is drawn in the second pass.
        if (toplevel->control && !win::decoration_has_alpha(toplevel)
            && toplevel->opacity() == 1.0) {
            data.clip = window->decorationShape().translated(toplevel->pos());
        }

        data.quads = window->buildQuads();

        // preparation step
        compositor.effects->prePaintWindow(window->effect.get(), data, m_expectedPresentTimestamp);
#if !defined(QT_NO_DEBUG)
        if (data.quads.isTransformed()) {
            qFatal("Pre-paint calls are not allowed to transform quads!");
        }
#endif
        if (!window->isPaintingEnabled()) {
            continue;
        }
        dirtyArea |= data.paint;
        // Schedule the window for painting
        phase2data.append(
            {window, data.paint, data.clip, static_cast<paint_type>(data.mask), data.quads});
    }

    // Save the part of the repaint region that's exclusively rendered to
    // bring a reused back buffer up to date. Then union the dirty region
    // with the repaint region.
    const QRegion repaintClip = repaint_region - dirtyArea;
    dirtyArea |= repaint_region;

    auto const& space_size = kwinApp()->get_base().topology.size;
    const QRegion displayRegion(0, 0, space_size.width(), space_size.height());
    bool fullRepaint(dirtyArea == displayRegion); // spare some expensive region operations
    if (!fullRepaint) {
        extendPaintRegion(dirtyArea, opaqueFullscreen);
        fullRepaint = (dirtyArea == displayRegion);
    }

    QRegion allclips, upperTranslucentDamage;
    upperTranslucentDamage = repaint_region;

    // This is the occlusion culling pass
    for (int i = phase2data.count() - 1; i >= 0; --i) {
        Phase2Data* data = &phase2data[i];

        if (fullRepaint) {
            data->region = displayRegion;
        } else {
            data->region |= upperTranslucentDamage;
        }

        // subtract the parts which will possibly been drawn as part of
        // a higher opaque window
        data->region -= allclips;

        // Here we rely on WindowPrePaintData::setTranslucent() to remove
        // the clip if needed.
        if (!data->clip.isEmpty() && !(data->mask & paint_type::window_translucent)) {
            // clip away the opaque regions for all windows below this one
            allclips |= data->clip;
            // extend the translucent damage for windows below this by remaining (translucent)
            // regions
            if (!fullRepaint) {
                upperTranslucentDamage |= data->region - data->clip;
            }
        } else if (!fullRepaint) {
            upperTranslucentDamage |= data->region;
        }
    }

    QRegion paintedArea;
    // Fill any areas of the root window not covered by opaque windows
    if (!(orig_mask & paint_type::screen_background_first)) {
        paintedArea = dirtyArea - allclips;
        paintBackground(paintedArea);
    }

    // Now walk the list bottom to top and draw the windows.
    for (int i = 0; i < phase2data.count(); ++i) {
        Phase2Data* data = &phase2data[i];

        // add all regions which have been drawn so far
        paintedArea |= data->region;
        data->region = paintedArea;

        paintWindow(data->window, data->mask, data->region, data->quads);
    }

    if (fullRepaint) {
        painted_region = displayRegion;
        damaged_region = displayRegion - repaintClip;
    } else {
        painted_region |= paintedArea;

        // Clip the repainted region from the damaged region.
        // It's important that we don't add the union of the damaged region
        // and the repainted region to the damage history. Otherwise the
        // repaint region will grow with every frame until it eventually
        // covers the whole back buffer, at which point we're always doing
        // full repaints.
        damaged_region = paintedArea - repaintClip;
    }
}

void scene::init_remnant(Toplevel& remnant)
{
    assert(remnant.render);
    remnant.render->updateToplevel(&remnant);

    if (auto shadow = remnant.render->shadow()) {
        shadow->m_topLevel = &remnant;
        QObject::connect(remnant.qobject.get(),
                         &win::window_qobject::frame_geometry_changed,
                         shadow,
                         &shadow::geometryChanged);
    }
}

void scene::windowGeometryShapeChanged(Toplevel* c)
{
    if (!c->render) {
        // This is ok, shape is not valid by default.
        return;
    }
    c->render->invalidateQuadsCache();
}

void scene::createStackingOrder(std::deque<Toplevel*> const& toplevels)
{
    // TODO: cache the stacking_order in case it has not changed
    for (auto const& c : toplevels) {
        assert(c->render);
        stacking_order.push_back(c->render.get());
    }
}

void scene::clearStackingOrder()
{
    stacking_order.clear();
}

static window* s_recursionCheck = nullptr;

void scene::paintWindow(window* w, paint_type mask, QRegion region, WindowQuadList quads)
{
    // no painting outside visible screen (and no transformations)
    region &= QRect({}, kwinApp()->get_base().topology.size);
    if (region.isEmpty()) // completely clipped
        return;

    if (s_recursionCheck == w) {
        return;
    }

    WindowPaintData data(w->effect.get(), screenProjectionMatrix());
    data.quads = quads;
    compositor.effects->paintWindow(w->effect.get(), static_cast<int>(mask), region, data);

    // paint thumbnails on top of window
    paintWindowThumbnails(w, region, data.opacity(), data.brightness(), data.saturation());
    // and desktop thumbnails
    paintDesktopThumbnails(w);
}

static void adjustClipRegion(basic_thumbnail_item* item, QRegion& clippingRegion)
{
    if (item->clip() && item->clipTo()) {
        // the x/y positions of the parent item are not correct. The margins are added, though the
        // size seems fine that's why we have to get the offset by inspecting the anchors properties
        QQuickItem* parentItem = item->clipTo();
        QPointF offset;
        QVariant anchors = parentItem->property("anchors");
        if (anchors.isValid()) {
            if (QObject* anchorsObject = anchors.value<QObject*>()) {
                offset.setX(anchorsObject->property("leftMargin").toReal());
                offset.setY(anchorsObject->property("topMargin").toReal());
            }
        }
        QRectF rect = QRectF(parentItem->position() - offset,
                             QSizeF(parentItem->width(), parentItem->height()));
        if (QQuickItem* p = parentItem->parentItem()) {
            rect = p->mapRectToScene(rect);
        }
        clippingRegion
            &= rect.adjusted(0, 0, -1, -1).translated(item->window()->position()).toRect();
    }
}

void scene::paintWindowThumbnails(window* w,
                                  QRegion region,
                                  qreal opacity,
                                  qreal brightness,
                                  qreal saturation)
{
    auto wImpl = static_cast<effects_window_impl*>(w->effect.get());
    for (QHash<window_thumbnail_item*, QPointer<effects_window_impl>>::const_iterator it
         = wImpl->thumbnails().constBegin();
         it != wImpl->thumbnails().constEnd();
         ++it) {
        if (it.value().isNull()) {
            continue;
        }
        window_thumbnail_item* item = it.key();
        if (!item->isVisible()) {
            continue;
        }
        auto thumb = it.value().data();
        WindowPaintData thumbData(thumb, screenProjectionMatrix());
        thumbData.setOpacity(opacity);
        thumbData.setBrightness(brightness * item->brightness());
        thumbData.setSaturation(saturation * item->saturation());

        const QRect visualThumbRect(thumb->expandedGeometry());

        QSizeF size = QSizeF(visualThumbRect.size());
        size.scale(QSizeF(item->width(), item->height()), Qt::KeepAspectRatio);
        if (size.width() > visualThumbRect.width() || size.height() > visualThumbRect.height()) {
            size = QSizeF(visualThumbRect.size());
        }
        thumbData.setXScale(size.width() / static_cast<qreal>(visualThumbRect.width()));
        thumbData.setYScale(size.height() / static_cast<qreal>(visualThumbRect.height()));

        if (!item->window()) {
            continue;
        }

        const QPointF point = item->mapToScene(QPointF(0, 0));
        auto const win_pos = w->get_window()->pos();
        qreal x = point.x() + win_pos.x() + (item->width() - size.width()) / 2;
        qreal y = point.y() + win_pos.y() + (item->height() - size.height()) / 2;
        x -= thumb->x();
        y -= thumb->y();

        // compensate shadow topleft padding
        x += (thumb->x() - visualThumbRect.x()) * thumbData.xScale();
        y += (thumb->y() - visualThumbRect.y()) * thumbData.yScale();
        thumbData.setXTranslation(x);
        thumbData.setYTranslation(y);
        auto thumbMask = paint_type::window_transformed | paint_type::window_lanczos;
        if (thumbData.opacity() == 1.0) {
            thumbMask |= paint_type::window_opaque;
        } else {
            thumbMask |= paint_type::window_translucent;
        }
        QRegion clippingRegion = region;
        clippingRegion &= QRegion(wImpl->x(), wImpl->y(), wImpl->width(), wImpl->height());
        adjustClipRegion(item, clippingRegion);
        compositor.effects->drawWindow(
            thumb, static_cast<int>(thumbMask), clippingRegion, thumbData);
    }
}

void scene::paintDesktopThumbnails(window* w)
{
    auto wImpl = static_cast<effects_window_impl*>(w->effect.get());
    for (QList<desktop_thumbnail_item*>::const_iterator it
         = wImpl->desktopThumbnails().constBegin();
         it != wImpl->desktopThumbnails().constEnd();
         ++it) {
        desktop_thumbnail_item* item = *it;
        if (!item->isVisible()) {
            continue;
        }
        if (!item->window()) {
            continue;
        }
        s_recursionCheck = w;

        ScreenPaintData data;
        auto const& space_size = kwinApp()->get_base().topology.size;
        auto size = space_size;

        size.scale(item->width(), item->height(), Qt::KeepAspectRatio);
        data *= QVector2D(size.width() / double(space_size.width()),
                          size.height() / double(space_size.height()));

        const QPointF point = item->mapToScene(item->position());
        auto const win_pos = w->get_window()->pos();
        const qreal x = point.x() + win_pos.x() + (item->width() - size.width()) / 2;
        const qreal y = point.y() + win_pos.y() + (item->height() - size.height()) / 2;
        const QRect region = QRect(x, y, item->width(), item->height());

        QRegion clippingRegion = region;
        clippingRegion &= QRegion(wImpl->x(), wImpl->y(), wImpl->width(), wImpl->height());
        adjustClipRegion(item, clippingRegion);

        data += QPointF(x, y);
        auto const desktopMask = paint_type::screen_transformed | paint_type::window_transformed
            | paint_type::screen_background_first;
        paintDesktop(item->desktop(), desktopMask, clippingRegion, data);
        s_recursionCheck = nullptr;
    }
}

void scene::paintDesktop(int desktop, paint_type mask, const QRegion& region, ScreenPaintData& data)
{
    compositor.effects->paintDesktop(desktop, static_cast<int>(mask), region, data);
}

// the function that'll be eventually called by paintWindow() above
void scene::finalPaintWindow(effects_window_impl* w,
                             paint_type mask,
                             QRegion region,
                             WindowPaintData& data)
{
    compositor.effects->drawWindow(w, static_cast<int>(mask), region, data);
}

// will be eventually called from drawWindow()
void scene::finalDrawWindow(effects_window_impl* w,
                            paint_type mask,
                            QRegion region,
                            WindowPaintData& data)
{
    if (kwinApp()->is_screen_locked() && !w->window()->isLockScreen()
        && !w->window()->isInputMethod()) {
        return;
    }
    w->sceneWindow()->performPaint(mask, region, data);
}

void scene::extendPaintRegion(QRegion& region, bool opaqueFullscreen)
{
    Q_UNUSED(region);
    Q_UNUSED(opaqueFullscreen);
}

bool scene::hasSwapEvent() const
{
    return false;
}

bool scene::makeOpenGLContextCurrent()
{
    return false;
}

void scene::doneOpenGLContextCurrent()
{
}

bool scene::supportsSurfacelessContext() const
{
    return false;
}

void scene::triggerFence()
{
}

QMatrix4x4 scene::screenProjectionMatrix() const
{
    return QMatrix4x4();
}

xcb_render_picture_t scene::xrenderBufferPicture() const
{
    return XCB_RENDER_PICTURE_NONE;
}

QPainter* scene::scenePainter() const
{
    return nullptr;
}

QVector<QByteArray> scene::openGLPlatformInterfaceExtensions() const
{
    return QVector<QByteArray>{};
}

QRect scene::renderTargetRect() const
{
    return m_renderTargetRect;
}

void scene::setRenderTargetRect(const QRect& rect)
{
    m_renderTargetRect = rect;
}

qreal scene::renderTargetScale() const
{
    return m_renderTargetScale;
}

void scene::setRenderTargetScale(qreal scale)
{
    m_renderTargetScale = scale;
}

QRegion scene::mapToRenderTarget(const QRegion& region) const
{
    QRegion result;
    for (auto const& rect : region) {
        result += QRect((rect.x() - m_renderTargetRect.x()) * m_renderTargetScale,
                        (rect.y() - m_renderTargetRect.y()) * m_renderTargetScale,
                        rect.width() * m_renderTargetScale,
                        rect.height() * m_renderTargetScale);
    }
    return result;
}

}
