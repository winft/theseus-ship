/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"
#include "effect/window_group_impl.h"
#include "shadow.h"
#include "singleton_interface.h"
#include "types.h"

#include "base/wayland/screen_lock.h"
#include "win/damage.h"
#include "win/deco/renderer.h"
#include "win/geo.h"
#include "win/space_qobject.h"

#include <render/effect/interface/effect_quick_view.h>
#include <render/effect/interface/paint_clipper.h>
#include <render/effect/interface/paint_data.h>

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
    using platform_t = Platform;
    using space_t = typename Platform::base_t::space_t;
    using window_t = typename Platform::compositor_t::window_t;
    using effect_window_t = typename window_t::effect_window_t;
    using effect_window_group_t = effect_window_group_impl<typename space_t::window_group_t>;
    using buffer_t = buffer<window_t>;
    using output_t = typename Platform::base_t::output_t;

    explicit scene(Platform& platform)
        : platform{platform}
    {
        singleton_interface::supports_surfaceless_context
            = [this] { return supportsSurfacelessContext(); };
    }

    ~scene() override
    {
        singleton_interface::supports_surfaceless_context = {};
    }

    virtual bool isOpenGl() const = 0;

    virtual int64_t paint_output(output_t* /*output*/,
                                 QRegion /*damage*/,
                                 std::deque<typename window_t::ref_t> const& /*ref_wins*/,
                                 std::chrono::milliseconds /*presentTime*/)
    {
        assert(false);
        return 0;
    }

    virtual void end_paint()
    {
    }

    virtual std::unique_ptr<window_t> createWindow(typename window_t::ref_t ref_win) = 0;

    /**
     * @brief Creates the scene specific shadow subclass.
     *
     * An implementing class has to create a proper instance. It is not allowed to
     * return @c null.
     */
    virtual std::unique_ptr<shadow<window_t>> createShadow(window_t* win) = 0;
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

    virtual void triggerFence()
    {
    }

    virtual std::unique_ptr<win::deco::render_injector> create_deco(win::deco::render_window window)
        = 0;

    /**
     * Whether the scene is able to drive animations.
     * This is used as a hint to the effects system which effects can be supported.
     * If the scene performs software rendering it is supposed to return @c false,
     * if rendering is hardware accelerated it should return @c true.
     */
    virtual bool animationsSupported() const = 0;

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

    // shape/size of a window changed
    template<typename RefWin>
    void windowGeometryShapeChanged(RefWin* ref_win)
    {
        if (!ref_win->render) {
            // This is ok, shape is not valid by default.
            return;
        }
        ref_win->render->invalidateQuadsCache();
    }

    Platform& platform;
    scene_windowing_integration windowing_integration;

    uint32_t window_id{0};

    void createStackingOrder(std::deque<typename window_t::ref_t> const& ref_wins)
    {
        // TODO: cache the stacking_order in case it has not changed
        for (auto const& ref_win : ref_wins) {
            std::visit(overload{[this](auto&& ref_win) {
                           assert(ref_win->render);
                           stacking_order.push_back(ref_win->render.get());
                       }},
                       ref_win);
        }
    }

    void clearStackingOrder()
    {
        stacking_order.clear();
    }

    // shared implementation, starts painting the screen
    void paintScreen(effect::render_data& render,
                     paint_type& mask,
                     const QRegion& damage,
                     const QRegion& repaint,
                     QRegion* updateRegion,
                     QRegion* validRegion,
                     std::chrono::milliseconds presentTime)
    {
        auto const& space_size = platform.base.topology.size;
        const QRegion displayRegion(0, 0, space_size.width(), space_size.height());
        mask = (damage == displayRegion) ? paint_type::none : paint_type::screen_region;

        assert(repaint_output);
        auto effect_screen = platform.compositor->effects->findScreen(repaint_output->name());
        assert(effect_screen);

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

        effect::screen_prepaint_data pre_data {
            .screen = *effect_screen,
            .paint = {
                .mask = static_cast<int>(mask),
                .region = region,
            },
            .render = render,
            .present_time = m_expectedPresentTimestamp,
        };

        platform.compositor->effects->prePaintScreen(pre_data);

        mask = static_cast<paint_type>(pre_data.paint.mask);
        region = pre_data.paint.region;
        render.targets = pre_data.render.targets;

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
            paintBackground(region, render.projection * render.view);
        }

        effect::screen_paint_data data{
            .screen = effect_screen,
            .paint = {
                .mask = static_cast<int>(mask),
                .region = region,
            },
            .render = render,
        };

        platform.compositor->effects->paintScreen(data);
        render.targets = data.render.targets;

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

    // called after all effects had their paintScreen() called
    void finalPaintScreen(paint_type mask, effect::screen_paint_data& data)
    {
        if (flags(
                mask
                & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))) {
            paintGenericScreen(mask, data);
        } else {
            paintSimpleScreen(mask, data.paint.region, data.render);
        }
    }

    // saved data for 2nd pass of optimized screen painting
    struct Phase2Data {
        window_t* window = nullptr;
        QRegion region;
        QRegion clip;
        paint_type mask{paint_type::none};
        WindowQuadList quads;
    };

    // The generic (unoptimized) painting code that can handle even transformations. It simply
    // paints bottom-to-top.
    virtual void paintGenericScreen(paint_type mask, effect::screen_paint_data& data)
    {
        if (!(mask & paint_type::screen_background_first)) {
            paintBackground(infiniteRegion(), data.render.projection * data.render.view);
        }

        QVector<Phase2Data> phase2;
        phase2.reserve(stacking_order.size());

        for (auto const& win : stacking_order) {
            // Bottom to top.
            //
            if (!win->isPaintingEnabled()) {
                continue;
            }

            // Reset the repaint_region.
            // This has to be done here because many effects schedule a repaint for
            // the next frame within Effects::prePaintWindow.
            std::visit(overload{[this](auto&& win) { win::reset_repaints(*win, repaint_output); }},
                       *win->ref_win);

            effect::window_prepaint_data win_data{
                .window = *win->effect,
                .paint = {
                    .mask = static_cast<int>(mask
                            | (win->isOpaque() ? paint_type::window_opaque
                                               : paint_type::window_translucent)),
                    // no clipping, so doesn't really matter
                    .region = infiniteRegion(),
                },
                .clip = {},
                .quads = win->buildQuads(),
                .present_time = m_expectedPresentTimestamp,
            };

            // preparation step
            platform.compositor->effects->prePaintWindow(win_data);

#if !defined(QT_NO_DEBUG)
            if (win_data.quads.isTransformed()) {
                qFatal("Pre-paint calls are not allowed to transform quads!");
            }
#endif

            phase2.append({win,
                           infiniteRegion(),
                           win_data.clip,
                           static_cast<paint_type>(win_data.paint.mask),
                           win_data.quads});
        }

        for (auto const& data2 : phase2) {
            paintWindow(data.render, data2.window, data2.mask, data2.region, data2.quads);
        }

        auto const& space_size = platform.base.topology.size;
        damaged_region = QRegion(0, 0, space_size.width(), space_size.height());
    }

    template<typename RefWin>
    void prepare_simple_window_paint(RefWin& ref_win,
                                     paint_type const orig_mask,
                                     QRegion const& region,
                                     QRegion& dirtyArea,
                                     bool& opaqueFullscreen,
                                     QVector<Phase2Data>& phase2data)
    {
        auto win = ref_win.render.get();
        if (!win->isPaintingEnabled()) {
            return;
        }

        effect::window_prepaint_data data{
            .window = *win->effect,
            .paint
            = {.mask = static_cast<int>(orig_mask
                                        | (win->isOpaque() ? paint_type::window_opaque
                                                           : paint_type::window_translucent)),
               .region = region | win::repaints(ref_win)},
            .present_time = m_expectedPresentTimestamp,
        };

        // Reset the repaint_region.
        // This has to be done here because many effects schedule a repaint for
        // the next frame within Effects::prePaintWindow.
        win::reset_repaints(ref_win, repaint_output);

        opaqueFullscreen = false;

        // TODO: do we care about unmanged windows here (maybe input windows?)
        if (win->isOpaque()) {
            if (ref_win.control) {
                opaqueFullscreen = ref_win.control->fullscreen;
            }
            data.clip |= win::content_render_region(&ref_win).translated(ref_win.geo.pos()
                                                                         + win->bufferOffset());
        } else if (win::has_alpha(ref_win) && ref_win.opacity() == 1.0) {
            auto const clientShape = win::content_render_region(&ref_win).translated(
                win::frame_to_render_pos(&ref_win, ref_win.geo.pos()));
            auto const opaqueShape = ref_win.render_data.opaque_region.translated(
                win::frame_to_client_pos(&ref_win, ref_win.geo.pos()) - ref_win.geo.pos());
            data.clip = clientShape & opaqueShape;
            if (clientShape == opaqueShape) {
                data.paint.mask = static_cast<int>(orig_mask | paint_type::window_opaque);
            }
        } else {
            data.clip = QRegion();
        }

        // Clip out decoration without alpha when window has not set additional opacity by us.
        // The decoration is drawn in the second pass.
        if (ref_win.control && !win::decoration_has_alpha(&ref_win) && ref_win.opacity() == 1.0) {
            data.clip = win->decorationShape().translated(ref_win.geo.pos());
        }

        data.quads = win->buildQuads();

        // preparation step
        platform.compositor->effects->prePaintWindow(data);

#if !defined(QT_NO_DEBUG)
        if (data.quads.isTransformed()) {
            qFatal("Pre-paint calls are not allowed to transform quads!");
        }
#endif

        dirtyArea |= data.paint.region;

        // Schedule the window for painting
        phase2data.append({win,
                           data.paint.region,
                           data.clip,
                           static_cast<paint_type>(data.paint.mask),
                           data.quads});
    }

    // The optimized case without any transformations at all. It can paint only the requested region
    // and can use clipping to reduce painting and improve performance.
    void
    paintSimpleScreen(paint_type orig_mask, QRegion const& region, effect::render_data& render_data)
    {
        Q_ASSERT((orig_mask
                  & (paint_type::screen_transformed | paint_type::screen_with_transformed_windows))
                 == paint_type::none);
        QVector<Phase2Data> phase2data;
        phase2data.reserve(stacking_order.size());

        QRegion dirtyArea = region;
        bool opaqueFullscreen = false;

        // Traverse the scene windows from bottom to top.
        for (auto&& win : stacking_order) {
            std::visit(
                overload{[&](auto&& ref_win) {
                    prepare_simple_window_paint(
                        *ref_win, orig_mask, region, dirtyArea, opaqueFullscreen, phase2data);
                }},
                *win->ref_win);
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
            paintBackground(paintedArea, render_data.projection * render_data.view);
        }

        // Now walk the list bottom to top and draw the windows.
        for (int i = 0; i < phase2data.count(); ++i) {
            Phase2Data* data = &phase2data[i];

            // add all regions which have been drawn so far
            paintedArea |= data->region;
            data->region = paintedArea;

            paintWindow(render_data, data->window, data->mask, data->region, data->quads);
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
    virtual void paintBackground(QRegion const& region, QMatrix4x4 const& projection) = 0;

    // called after all effects had their paintWindow() called, eventually by paintWindow() below
    void finalPaintWindow(effect::window_paint_data& data)
    {
        platform.compositor->effects->drawWindow(data);
    }

    // shared implementation, starts painting the window
    void paintWindow(effect::render_data& render_data,
                     window_t* win,
                     paint_type mask,
                     QRegion region,
                     WindowQuadList quads)
    {
        // no painting outside visible screen (and no transformations)
        region &= QRect({}, platform.base.topology.size);
        if (region.isEmpty()) {
            // completely clipped
            return;
        }

        effect::window_paint_data data{
            *win->effect,
            {
                .mask = static_cast<int>(mask),
                .region = region,
            },
            quads,
            render_data,
        };

        platform.compositor->effects->paintWindow(data);
        render_data.targets = data.render.targets;
    }

    // called after all effects had their drawWindow() called, eventually called from drawWindow()
    virtual void finalDrawWindow(effect::window_paint_data& data)
    {
        auto& eff_win = static_cast<effect_window_t&>(data.window);
        auto mask = static_cast<paint_type>(data.paint.mask);

        if (base::wayland::is_screen_locked(platform.base)) {
            if (!std::visit(
                    overload{[](auto&& win) {
                        auto do_draw{false};
                        if constexpr (requires(decltype(win) win) { win->isLockScreen(); }) {
                            do_draw |= win->isLockScreen();
                        }
                        if constexpr (requires(decltype(win) win) { win->isInputMethod(); }) {
                            do_draw |= win->isInputMethod();
                        }
                        return do_draw;
                    }},
                    *eff_win.window.ref_win)) {
                return;
            }
        }
        eff_win.window.performPaint(mask, data);
    }

    // let the scene decide whether it's better to paint more of the screen, eg. in order to allow a
    // buffer swap the default is NOOP
    virtual void extendPaintRegion(QRegion& /*region*/, bool /*opaqueFullscreen*/)
    {
    }

    virtual void paintEffectQuickView(EffectQuickView* view) = 0;

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

    // The output currently being repainted.
    output_t* repaint_output{nullptr};

private:
    std::chrono::milliseconds m_expectedPresentTimestamp = std::chrono::milliseconds::zero();

    // Windows stacking order of the current paint run.
    std::vector<window_t*> stacking_order;
};

}
