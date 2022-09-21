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

#include "buffer.h"
#include "shadow.h"
#include "singleton_interface.h"
#include "thumbnail_item.h"
#include "types.h"
#include "win/deco/renderer.h"

#include "win/damage.h"
#include "win/geo.h"
#include "win/space_qobject.h"

#include <kwineffects/effect_quick_view.h>
#include <kwineffects/paint_clipper.h>
#include <kwineffects/paint_data.h>

#include <QMatrix4x4>
#include <QQuickWindow>
#include <chrono>
#include <deque>
#include <memory>
#include <xcb/render.h>

namespace KWin::render
{

struct scene_windowing_integration {
    std::function<void(void)> handle_viewport_limits_alarm;
};

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
template<typename Platform>
class scene : public QObject
{
public:
    using space_t = typename Platform::base_t::space_t;
    using window_t = typename space_t::window_t::render_t;
    using effect_window_t = typename window_t::effect_window_t;
    using buffer_t = buffer<window_t>;
    using output_t = typename Platform::base_t::output_t;

    explicit scene(Platform& platform)
        : platform{platform}
    {
        singleton_interface::supports_surfaceless_context
            = [this] { return supportsSurfacelessContext(); };

        QObject::connect(platform.base.space->qobject.get(),
                         &win::space_qobject::remnant_created,
                         this,
                         [this](auto win_id) {
                             auto remnant = this->platform.base.space->windows_map.at(win_id);
                             init_remnant(*remnant);
                         });
    }

    ~scene() override
    {
        singleton_interface::supports_surfaceless_context = {};
    }

    virtual CompositingType compositingType() const = 0;

    /**
     * The entry point for the main part of the painting pass. Repaints the given screen areas.
     *
     * @param damage is the area that needs to be repaint
     * @param windows provides the stacking order
     * @return the elapsed time in ns
     */
    virtual int64_t paint(QRegion /*damage*/,
                          std::deque<typename window_t::ref_t*> const& /*windows*/,
                          std::chrono::milliseconds /*presentTime*/)
    {
        assert(false);
        return 0;
    }

    virtual int64_t paint_output(output_t* /*output*/,
                                 QRegion /*damage*/,
                                 std::deque<typename window_t::ref_t*> const& /*windows*/,
                                 std::chrono::milliseconds /*presentTime*/)
    {
        assert(false);
        return 0;
    }

    virtual std::unique_ptr<window_t> createWindow(typename window_t::ref_t* toplevel) = 0;

    /**
     * @brief Creates the scene specific shadow subclass.
     *
     * An implementing class has to create a proper instance. It is not allowed to
     * return @c null.
     *
     * @param toplevel The reference window for which the Shadow needs to be created.
     */
    virtual std::unique_ptr<shadow<window_t>> createShadow(window_t* window) = 0;
    /**
     * Method invoked when the screen geometry is changed.
     * Reimplementing classes should also invoke the parent method
     * as it takes care of resizing the overlay window.
     * @param size The new screen geometry size
     */
    virtual void handle_screen_geometry_change(QSize const& size) = 0;

    // There's nothing to paint (adjust time_diff later). Painting pass is optimized away.
    virtual void idle()
    {
    }

    virtual bool hasSwapEvent() const
    {
        return false;
    }

    virtual bool makeOpenGLContextCurrent()
    {
        return false;
    }

    virtual void doneOpenGLContextCurrent()
    {
    }

    virtual bool supportsSurfacelessContext() const
    {
        return false;
    }

    virtual QMatrix4x4 screenProjectionMatrix() const
    {
        return QMatrix4x4();
    }

    virtual void triggerFence()
    {
    }

    virtual win::deco::renderer<win::deco::client_impl<typename window_t::ref_t>>*
    createDecorationRenderer(win::deco::client_impl<typename window_t::ref_t>*)
        = 0;

    /**
     * Whether the scene is able to drive animations.
     * This is used as a hint to the effects system which effects can be supported.
     * If the scene performs software rendering it is supposed to return @c false,
     * if rendering is hardware accelerated it should return @c true.
     */
    virtual bool animationsSupported() const = 0;

    /**
     * The render buffer used by an XRender based compositor scene.
     * Default implementation returns XCB_RENDER_PICTURE_NONE
     */
    virtual xcb_render_picture_t xrenderBufferPicture() const
    {
        return XCB_RENDER_PICTURE_NONE;
    }

    /**
     * The QPainter used by a QPainter based compositor scene.
     * Default implementation returns @c nullptr;
     */
    virtual QPainter* scenePainter() const
    {
        return nullptr;
    }

    /**
     * The backend specific extensions (e.g. EGL/GLX extensions).
     *
     * Not the OpenGL (ES) extension!
     *
     * Default implementation returns empty list
     */
    virtual QVector<QByteArray> openGLPlatformInterfaceExtensions() const
    {
        return QVector<QByteArray>{};
    }

    QRegion mapToRenderTarget(const QRegion& region) const
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

    // shape/size of a window changed
    void windowGeometryShapeChanged(typename window_t::ref_t* c)
    {
        if (!c->render) {
            // This is ok, shape is not valid by default.
            return;
        }
        c->render->invalidateQuadsCache();
    }

    void init_remnant(typename window_t::ref_t& remnant)
    {
        assert(remnant.render);
        remnant.render->ref_win = &remnant;

        if (auto shadow = remnant.render->shadow()) {
            QObject::connect(remnant.qobject.get(),
                             &win::window_qobject::frame_geometry_changed,
                             shadow,
                             &std::remove_pointer_t<decltype(shadow)>::geometryChanged);
        }
    }

    Platform& platform;
    scene_windowing_integration windowing_integration;

    uint32_t window_id{0};

    QRect m_renderTargetRect;
    qreal m_renderTargetScale = 1;

    void createStackingOrder(std::deque<typename window_t::ref_t*> const& toplevels)
    {
        // TODO: cache the stacking_order in case it has not changed
        for (auto const& c : toplevels) {
            assert(c->render);
            stacking_order.push_back(c->render.get());
        }
    }

    void clearStackingOrder()
    {
        stacking_order.clear();
    }

    // shared implementation, starts painting the screen
    void paintScreen(paint_type& mask,
                     const QRegion& damage,
                     const QRegion& repaint,
                     QRegion* updateRegion,
                     QRegion* validRegion,
                     std::chrono::milliseconds presentTime,
                     const QMatrix4x4& projection = QMatrix4x4())
    {
        auto const& space_size = platform.base.topology.size;
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
        platform.compositor->effects->startPaint();

        QRegion region = damage;

        ScreenPrePaintData pdata;
        pdata.mask = static_cast<int>(mask);
        pdata.paint = region;

        platform.compositor->effects->prePaintScreen(pdata, m_expectedPresentTimestamp);

        mask = static_cast<paint_type>(pdata.mask);
        region = pdata.paint;

        if (flags(
                mask
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
                             repaint_output
                                 ? platform.compositor->effects->findScreen(repaint_output->name())
                                 : nullptr);
        platform.compositor->effects->paintScreen(static_cast<int>(mask), region, data);

        for (auto const& w : stacking_order) {
            platform.compositor->effects->postPaintWindow(w->effect.get());
        }

        platform.compositor->effects->postPaintScreen();

        // make sure not to go outside of the screen area
        *updateRegion = damaged_region;
        *validRegion = (region | painted_region) & displayRegion;

        repaint_region = QRegion();
        damaged_region = QRegion();

        // make sure all clipping is restored
        Q_ASSERT(!PaintClipper::clip());
    }

    // Render cursor texture in case hardware cursor is disabled/non-applicable
    virtual void paintCursor() = 0;

    // called after all effects had their paintScreen() called
    void finalPaintScreen(paint_type mask, QRegion region, ScreenPaintData& data)
    {
        if (flags(
                mask
                & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))) {
            paintGenericScreen(mask, data);
        } else {
            paintSimpleScreen(mask, region);
        }
    }

    // The generic (unoptimized) painting code that can handle even transformations. It simply
    // paints bottom-to-top.
    virtual void paintGenericScreen(paint_type orig_mask, ScreenPaintData /*data*/)
    {
        if (!(orig_mask & paint_type::screen_background_first)) {
            paintBackground(infiniteRegion());
        }
        QVector<Phase2Data> phase2;
        phase2.reserve(stacking_order.size());
        for (auto const& w : stacking_order) {
            // bottom to top
            auto topw = w->ref_win;

            // Reset the repaint_region.
            // This has to be done here because many effects schedule a repaint for
            // the next frame within Effects::prePaintWindow.
            win::reset_repaints(*topw, repaint_output);

            WindowPrePaintData data;
            data.mask = static_cast<int>(
                orig_mask
                | (w->isOpaque() ? paint_type::window_opaque : paint_type::window_translucent));
            w->resetPaintingEnabled();
            data.paint = infiniteRegion(); // no clipping, so doesn't really matter
            data.clip = QRegion();
            data.quads = w->buildQuads();

            // preparation step
            platform.compositor->effects->prePaintWindow(
                w->effect.get(), data, m_expectedPresentTimestamp);
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

        auto const& space_size = platform.base.topology.size;
        damaged_region = QRegion(0, 0, space_size.width(), space_size.height());
    }

    // The optimized case without any transformations at all. It can paint only the requested region
    // and can use clipping to reduce painting and improve performance.
    virtual void paintSimpleScreen(paint_type orig_mask, QRegion region)
    {
        Q_ASSERT((orig_mask
                  & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))
                 == paint_type::none);
        QVector<Phase2Data> phase2data;
        phase2data.reserve(stacking_order.size());

        QRegion dirtyArea = region;
        bool opaqueFullscreen = false;

        // Traverse the scene windows from bottom to top.
        for (auto&& window : stacking_order) {
            auto toplevel = window->ref_win;
            WindowPrePaintData data;
            data.mask = static_cast<int>(orig_mask
                                         | (window->isOpaque() ? paint_type::window_opaque
                                                               : paint_type::window_translucent));
            window->resetPaintingEnabled();
            data.paint = region;
            data.paint |= win::repaints(*toplevel);

            // Reset the repaint_region.
            // This has to be done here because many effects schedule a repaint for
            // the next frame within Effects::prePaintWindow.
            win::reset_repaints(*toplevel, repaint_output);

            opaqueFullscreen = false;

            // TODO: do we care about unmanged windows here (maybe input windows?)
            if (window->isOpaque()) {
                if (toplevel->control) {
                    opaqueFullscreen = toplevel->control->fullscreen;
                }
                data.clip |= win::content_render_region(toplevel).translated(
                    toplevel->geo.pos() + window->bufferOffset());
            } else if (win::has_alpha(*toplevel) && toplevel->opacity() == 1.0) {
                auto const clientShape = win::content_render_region(toplevel).translated(
                    win::frame_to_render_pos(toplevel, toplevel->geo.pos()));
                auto const opaqueShape = toplevel->opaque_region.translated(
                    win::frame_to_client_pos(toplevel, toplevel->geo.pos()) - toplevel->geo.pos());
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
                data.clip = window->decorationShape().translated(toplevel->geo.pos());
            }

            data.quads = window->buildQuads();

            // preparation step
            platform.compositor->effects->prePaintWindow(
                window->effect.get(), data, m_expectedPresentTimestamp);
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

        auto const& space_size = platform.base.topology.size;
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

    // paint the background (not the desktop background - the whole background)
    virtual void paintBackground(QRegion region) = 0;

    // called after all effects had their paintWindow() called, eventually by paintWindow() below
    void
    finalPaintWindow(effect_window_t* w, paint_type mask, QRegion region, WindowPaintData& data)
    {
        platform.compositor->effects->drawWindow(w, static_cast<int>(mask), region, data);
    }

    // shared implementation, starts painting the window
    virtual void paintWindow(window_t* w, paint_type mask, QRegion region, WindowQuadList quads)
    {
        // no painting outside visible screen (and no transformations)
        region &= QRect({}, platform.base.topology.size);
        if (region.isEmpty()) // completely clipped
            return;

        if (s_recursionCheck == w) {
            return;
        }

        WindowPaintData data(w->effect.get(), screenProjectionMatrix());
        data.quads = quads;
        platform.compositor->effects->paintWindow(
            w->effect.get(), static_cast<int>(mask), region, data);

        // paint thumbnails on top of window
        paintWindowThumbnails(w, region, data.opacity(), data.brightness(), data.saturation());
        // and desktop thumbnails
        paintDesktopThumbnails(w);
    }

    // called after all effects had their drawWindow() called, eventually called from drawWindow()
    virtual void
    finalDrawWindow(effect_window_t* w, paint_type mask, QRegion region, WindowPaintData& data)
    {
        if (kwinApp()->is_screen_locked() && !w->window.ref_win->isLockScreen()
            && !w->window.ref_win->isInputMethod()) {
            return;
        }
        w->window.performPaint(mask, region, data);
    }

    // let the scene decide whether it's better to paint more of the screen, eg. in order to allow a
    // buffer swap the default is NOOP
    virtual void extendPaintRegion(QRegion& /*region*/, bool /*opaqueFullscreen*/)
    {
    }

    virtual void
    paintDesktop(int desktop, paint_type mask, const QRegion& region, ScreenPaintData& data)
    {
        platform.compositor->effects->paintDesktop(desktop, static_cast<int>(mask), region, data);
    }

    virtual void paintEffectQuickView(EffectQuickView* w) = 0;

    // saved data for 2nd pass of optimized screen painting
    struct Phase2Data {
        window_t* window = nullptr;
        QRegion region;
        QRegion clip;
        paint_type mask{paint_type::none};
        WindowQuadList quads;
    };
    // The region which actually has been painted by paintScreen() and should be
    // copied from the buffer to the screen. I.e. the region returned from scene::paintScreen().
    // Since prePaintWindow() can extend areas to paint, these changes would have to propagate
    // up all the way from paintSimpleScreen() up to paintScreen(), so save them here rather
    // than propagate them up in arguments.
    QRegion painted_region;
    // Additional damage that needs to be repaired to bring a reused back buffer up to date
    QRegion repaint_region;
    // The dirty region before it was unioned with repaint_region
    QRegion damaged_region;

    /**
     * The output currently being repainted. Only relevant for per-output painting.
     */
    output_t* repaint_output{nullptr};

private:
    void paintWindowThumbnails(window_t* w,
                               QRegion region,
                               qreal opacity,
                               qreal brightness,
                               qreal saturation)
    {
        auto wImpl = static_cast<effect_window_t*>(w->effect.get());
        for (auto it = wImpl->thumbnails().constBegin(); it != wImpl->thumbnails().constEnd();
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
            if (size.width() > visualThumbRect.width()
                || size.height() > visualThumbRect.height()) {
                size = QSizeF(visualThumbRect.size());
            }
            thumbData.setXScale(size.width() / static_cast<qreal>(visualThumbRect.width()));
            thumbData.setYScale(size.height() / static_cast<qreal>(visualThumbRect.height()));

            if (!item->window()) {
                continue;
            }

            const QPointF point = item->mapToScene(QPointF(0, 0));
            auto const win_pos = w->ref_win->geo.pos();
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
            platform.compositor->effects->drawWindow(
                thumb, static_cast<int>(thumbMask), clippingRegion, thumbData);
        }
    }

    void paintDesktopThumbnails(window_t* w)
    {
        auto wImpl = static_cast<effect_window_t*>(w->effect.get());
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
            auto const& space_size = platform.base.topology.size;
            auto size = space_size;

            size.scale(item->width(), item->height(), Qt::KeepAspectRatio);
            data *= QVector2D(size.width() / double(space_size.width()),
                              size.height() / double(space_size.height()));

            const QPointF point = item->mapToScene(item->position());
            auto const win_pos = w->ref_win->geo.pos();
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

    static void adjustClipRegion(basic_thumbnail_item* item, QRegion& clippingRegion)
    {
        if (item->clip() && item->clipTo()) {
            // the x/y positions of the parent item are not correct. The margins are added, though
            // the size seems fine that's why we have to get the offset by inspecting the anchors
            // properties
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

    std::chrono::milliseconds m_expectedPresentTimestamp = std::chrono::milliseconds::zero();

    // Windows stacking order of the current paint run.
    std::vector<window_t*> stacking_order;

    window_t* s_recursionCheck{nullptr};
};

}
