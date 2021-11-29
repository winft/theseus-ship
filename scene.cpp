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
 the PAINT_WINDOW_TRANSLUCENT flag. The paintWindow() function will then get
 the mask with this flag turned on and will also paint using transparency.

 The paint pass does the actual painting, based on the information collected
 using the pre-paint pass. After running through the effects' paintScreen()
 either paintGenericScreen() or optimized paintSimpleScreen() are called.
 Those call paintWindow() on windows (not necessarily all), possibly using
 clipping to optimize performance and calling paintWindow() first with only
 PAINT_WINDOW_OPAQUE to paint the opaque parts and then later
 with PAINT_WINDOW_TRANSLUCENT to paint the transparent parts. Function
 paintWindow() again goes through effects' paintWindow() until
 finalPaintWindow() is called, which calls the window's performPaint() to
 do the actual painting.

 The post-paint can be used for cleanups and is also used for scheduling
 repaints during the next painting pass for animations. Effects wanting to
 repaint certain parts can manually damage them during post-paint and repaint
 of these parts will be done during the next paint pass.
*/

#include "scene.h"

#include <QQuickWindow>
#include <QVector2D>

#include "base/output.h"
#include "effects.h"
#include "overlaywindow.h"
#include "screens.h"
#include "shadow.h"

#include "win/geo.h"
#include "win/scene.h"
#include "win/transient.h"

#include "win/x11/window.h"

#include "thumbnailitem.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render
{

//****************************************
// scene
//****************************************

scene::scene(QObject* parent)
    : QObject(parent)
{
}

scene::~scene()
{
    Q_ASSERT(m_windows.isEmpty());
}

qint64 scene::paint([[maybe_unused]] QRegion damage,
                    [[maybe_unused]] std::deque<Toplevel*> const& windows,
                    [[maybe_unused]] std::chrono::milliseconds presentTime)
{
    assert(false);
    return 0;
}

int64_t scene::paint([[maybe_unused]] base::output* output,
                     [[maybe_unused]] QRegion damage,
                     [[maybe_unused]] std::deque<Toplevel*> const& windows,
                     [[maybe_unused]] std::chrono::milliseconds presentTime)
{
    assert(false);
    return 0;
}

// returns mask and possibly modified region
void scene::paintScreen(int* mask,
                        const QRegion& damage,
                        const QRegion& repaint,
                        QRegion* updateRegion,
                        QRegion* validRegion,
                        std::chrono::milliseconds presentTime,
                        const QMatrix4x4& projection)
{
    const QSize& screenSize = screens()->size();
    const QRegion displayRegion(0, 0, screenSize.width(), screenSize.height());
    *mask = (damage == displayRegion) ? 0 : PAINT_SCREEN_REGION;

    if (Q_UNLIKELY(presentTime < m_expectedPresentTimestamp)) {
        qCDebug(KWIN_CORE,
                "Provided presentation timestamp is invalid: %ld (current: %ld)",
                presentTime.count(),
                m_expectedPresentTimestamp.count());
    } else {
        m_expectedPresentTimestamp = presentTime;
    }

    // preparation step
    static_cast<EffectsHandlerImpl*>(effects)->startPaint();

    QRegion region = damage;

    ScreenPrePaintData pdata;
    pdata.mask = *mask;
    pdata.paint = region;

    effects->prePaintScreen(pdata, m_expectedPresentTimestamp);
    *mask = pdata.mask;
    region = pdata.paint;

    if (*mask & (PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS)) {
        // Region painting is not possible with transformations,
        // because screen damage doesn't match transformed positions.
        *mask &= ~PAINT_SCREEN_REGION;
        region = infiniteRegion();
    } else if (*mask & PAINT_SCREEN_REGION) {
        // make sure not to go outside visible screen
        region &= displayRegion;
    } else {
        // whole screen, not transformed, force region to be full
        region = displayRegion;
    }

    painted_region = region;
    repaint_region = repaint;

    if (*mask & PAINT_SCREEN_BACKGROUND_FIRST) {
        paintBackground(region);
    }

    ScreenPaintData data(projection,
                         repaint_output ? effects->findScreen(repaint_output->name()) : nullptr);
    effects->paintScreen(*mask, region, data);

    for (auto const& w : qAsConst(stacking_order)) {
        effects->postPaintWindow(effectWindow(w));
    }

    effects->postPaintScreen();

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
void scene::finalPaintScreen(int mask, QRegion region, ScreenPaintData& data)
{
    if (mask & (PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS))
        paintGenericScreen(mask, data);
    else
        paintSimpleScreen(mask, region);
}

// The generic painting code that can handle even transformations.
// It simply paints bottom-to-top.
void scene::paintGenericScreen(int orig_mask, ScreenPaintData)
{
    if (!(orig_mask & PAINT_SCREEN_BACKGROUND_FIRST)) {
        paintBackground(infiniteRegion());
    }
    QVector<Phase2Data> phase2;
    phase2.reserve(stacking_order.size());
    for (auto const& w : qAsConst(stacking_order)) {
        // bottom to top
        auto topw = w->get_window();

        // Reset the repaint_region.
        // This has to be done here because many effects schedule a repaint for
        // the next frame within Effects::prePaintWindow.
        topw->resetRepaints(repaint_output);

        WindowPrePaintData data;
        data.mask = orig_mask | (w->isOpaque() ? PAINT_WINDOW_OPAQUE : PAINT_WINDOW_TRANSLUCENT);
        w->resetPaintingEnabled();
        data.paint = infiniteRegion(); // no clipping, so doesn't really matter
        data.clip = QRegion();
        data.quads = w->buildQuads();
        // preparation step
        effects->prePaintWindow(effectWindow(w), data, m_expectedPresentTimestamp);
#if !defined(QT_NO_DEBUG)
        if (data.quads.isTransformed()) {
            qFatal("Pre-paint calls are not allowed to transform quads!");
        }
#endif
        if (!w->isPaintingEnabled()) {
            continue;
        }
        phase2.append({w, infiniteRegion(), data.clip, data.mask, data.quads});
    }

    for (auto const& d : phase2) {
        paintWindow(d.window, d.mask, d.region, d.quads);
    }

    const QSize& screenSize = screens()->size();
    damaged_region = QRegion(0, 0, screenSize.width(), screenSize.height());
}

// The optimized case without any transformations at all.
// It can paint only the requested region and can use clipping
// to reduce painting and improve performance.
void scene::paintSimpleScreen(int orig_mask, QRegion region)
{
    Q_ASSERT((orig_mask & (PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS)) == 0);
    QVector<Phase2Data> phase2data;
    phase2data.reserve(stacking_order.size());

    QRegion dirtyArea = region;
    bool opaqueFullscreen = false;

    // Traverse the scene windows from bottom to top.
    for (int i = 0; i < stacking_order.count(); ++i) {
        auto window = stacking_order[i];
        auto toplevel = window->get_window();
        WindowPrePaintData data;
        data.mask
            = orig_mask | (window->isOpaque() ? PAINT_WINDOW_OPAQUE : PAINT_WINDOW_TRANSLUCENT);
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
                opaqueFullscreen = toplevel->control->fullscreen();
            }
            data.clip |= win::content_render_region(toplevel).translated(toplevel->pos()
                                                                         + window->bufferOffset());
        } else if (toplevel->hasAlpha() && toplevel->opacity() == 1.0) {
            auto const clientShape = win::content_render_region(toplevel).translated(
                win::frame_to_render_pos(toplevel, toplevel->pos()));
            auto const opaqueShape = toplevel->opaqueRegion().translated(
                win::frame_to_client_pos(toplevel, window->pos()) - window->pos());
            data.clip = clientShape & opaqueShape;
            if (clientShape == opaqueShape) {
                data.mask = orig_mask | PAINT_WINDOW_OPAQUE;
            }
        } else {
            data.clip = QRegion();
        }

        // Clip out decoration without alpha when window has not set additional opacity by us.
        // The decoration is drawn in the second pass.
        if (toplevel->control && !win::decoration_has_alpha(toplevel)
            && toplevel->opacity() == 1.0) {
            data.clip = window->decorationShape().translated(window->pos());
        }

        data.quads = window->buildQuads();
        // preparation step
        effects->prePaintWindow(effectWindow(window), data, m_expectedPresentTimestamp);
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
        phase2data.append({window, data.paint, data.clip, data.mask, data.quads});
    }

    // Save the part of the repaint region that's exclusively rendered to
    // bring a reused back buffer up to date. Then union the dirty region
    // with the repaint region.
    const QRegion repaintClip = repaint_region - dirtyArea;
    dirtyArea |= repaint_region;

    const QSize& screenSize = screens()->size();
    const QRegion displayRegion(0, 0, screenSize.width(), screenSize.height());
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
        if (!data->clip.isEmpty() && !(data->mask & PAINT_WINDOW_TRANSLUCENT)) {
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
    if (!(orig_mask & PAINT_SCREEN_BACKGROUND_FIRST)) {
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

void scene::addToplevel(Toplevel* c)
{
    Q_ASSERT(!m_windows.contains(c));
    auto w = createWindow(c);
    m_windows[c] = w;
    connect(c, &Toplevel::windowClosed, this, &scene::windowClosed);
    // A change of scale won't affect the geometry in compositor co-ordinates, but will affect the
    // window quads.
    if (c->surface()) {
        connect(c->surface(), &Wrapland::Server::Surface::committed, this, [this, c] {
            if (c->surface()->state().updates & Wrapland::Server::surface_change::scale) {
                windowGeometryShapeChanged(c);
            }
        });
    }
    connect(c, &Toplevel::screenScaleChanged, this, [this, c] { windowGeometryShapeChanged(c); });
    c->effectWindow()->setSceneWindow(w);
    win::update_shadow(c);
    w->updateShadow(win::shadow(c));
    connect(c, &Toplevel::shadowChanged, this, [w] { w->invalidateQuadsCache(); });
}

void scene::removeToplevel(Toplevel* toplevel)
{
    Q_ASSERT(m_windows.contains(toplevel));
    delete m_windows.take(toplevel);
    toplevel->effectWindow()->setSceneWindow(nullptr);
}

void scene::windowClosed(Toplevel* toplevel, Toplevel* deleted)
{
    if (!deleted) {
        removeToplevel(toplevel);
        return;
    }

    Q_ASSERT(m_windows.contains(toplevel));
    auto window = m_windows.take(toplevel);
    window->updateToplevel(deleted);
    if (window->shadow()) {
        window->shadow()->setToplevel(deleted);
    }
    m_windows[deleted] = window;
}

void scene::windowGeometryShapeChanged(Toplevel* c)
{
    if (!m_windows.contains(c)) // this is ok, shape is not valid by default
        return;
    auto w = m_windows[c];
    w->invalidateQuadsCache();
}

void scene::createStackingOrder(std::deque<Toplevel*> const& toplevels)
{
    // TODO: cache the stacking_order in case it has not changed
    for (auto const& c : toplevels) {
        assert(m_windows.contains(c));
        stacking_order.append(m_windows[c]);
    }
}

void scene::clearStackingOrder()
{
    stacking_order.clear();
}

static window* s_recursionCheck = nullptr;

void scene::paintWindow(window* w, int mask, QRegion region, WindowQuadList quads)
{
    // no painting outside visible screen (and no transformations)
    const QSize& screenSize = screens()->size();
    region &= QRect(0, 0, screenSize.width(), screenSize.height());
    if (region.isEmpty()) // completely clipped
        return;
    if (w->get_window()->isDeleted() && w->get_window()->skipsCloseAnimation()) {
        // should not get painted
        return;
    }

    if (s_recursionCheck == w) {
        return;
    }

    WindowPaintData data(w->get_window()->effectWindow(), screenProjectionMatrix());
    data.quads = quads;
    effects->paintWindow(effectWindow(w), mask, region, data);
    // paint thumbnails on top of window
    paintWindowThumbnails(w, region, data.opacity(), data.brightness(), data.saturation());
    // and desktop thumbnails
    paintDesktopThumbnails(w);
}

static void adjustClipRegion(AbstractThumbnailItem* item, QRegion& clippingRegion)
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
    EffectWindowImpl* wImpl = static_cast<EffectWindowImpl*>(effectWindow(w));
    for (QHash<WindowThumbnailItem*, QPointer<EffectWindowImpl>>::const_iterator it
         = wImpl->thumbnails().constBegin();
         it != wImpl->thumbnails().constEnd();
         ++it) {
        if (it.value().isNull()) {
            continue;
        }
        WindowThumbnailItem* item = it.key();
        if (!item->isVisible()) {
            continue;
        }
        EffectWindowImpl* thumb = it.value().data();
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
        qreal x = point.x() + w->x() + (item->width() - size.width()) / 2;
        qreal y = point.y() + w->y() + (item->height() - size.height()) / 2;
        x -= thumb->x();
        y -= thumb->y();
        // compensate shadow topleft padding
        x += (thumb->x() - visualThumbRect.x()) * thumbData.xScale();
        y += (thumb->y() - visualThumbRect.y()) * thumbData.yScale();
        thumbData.setXTranslation(x);
        thumbData.setYTranslation(y);
        int thumbMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_LANCZOS;
        if (thumbData.opacity() == 1.0) {
            thumbMask |= PAINT_WINDOW_OPAQUE;
        } else {
            thumbMask |= PAINT_WINDOW_TRANSLUCENT;
        }
        QRegion clippingRegion = region;
        clippingRegion &= QRegion(wImpl->x(), wImpl->y(), wImpl->width(), wImpl->height());
        adjustClipRegion(item, clippingRegion);
        effects->drawWindow(thumb, thumbMask, clippingRegion, thumbData);
    }
}

void scene::paintDesktopThumbnails(window* w)
{
    EffectWindowImpl* wImpl = static_cast<EffectWindowImpl*>(effectWindow(w));
    for (QList<DesktopThumbnailItem*>::const_iterator it = wImpl->desktopThumbnails().constBegin();
         it != wImpl->desktopThumbnails().constEnd();
         ++it) {
        DesktopThumbnailItem* item = *it;
        if (!item->isVisible()) {
            continue;
        }
        if (!item->window()) {
            continue;
        }
        s_recursionCheck = w;

        ScreenPaintData data;
        const QSize& screenSize = screens()->size();
        QSize size = screenSize;

        size.scale(item->width(), item->height(), Qt::KeepAspectRatio);
        data *= QVector2D(size.width() / double(screenSize.width()),
                          size.height() / double(screenSize.height()));
        const QPointF point = item->mapToScene(item->position());
        const qreal x = point.x() + w->x() + (item->width() - size.width()) / 2;
        const qreal y = point.y() + w->y() + (item->height() - size.height()) / 2;
        const QRect region = QRect(x, y, item->width(), item->height());
        QRegion clippingRegion = region;
        clippingRegion &= QRegion(wImpl->x(), wImpl->y(), wImpl->width(), wImpl->height());
        adjustClipRegion(item, clippingRegion);
        data += QPointF(x, y);
        const int desktopMask
            = PAINT_SCREEN_TRANSFORMED | PAINT_WINDOW_TRANSFORMED | PAINT_SCREEN_BACKGROUND_FIRST;
        paintDesktop(item->desktop(), desktopMask, clippingRegion, data);
        s_recursionCheck = nullptr;
    }
}

void scene::paintDesktop(int desktop, int mask, const QRegion& region, ScreenPaintData& data)
{
    static_cast<EffectsHandlerImpl*>(effects)->paintDesktop(desktop, mask, region, data);
}

// the function that'll be eventually called by paintWindow() above
void scene::finalPaintWindow(EffectWindowImpl* w, int mask, QRegion region, WindowPaintData& data)
{
    effects->drawWindow(w, mask, region, data);
}

// will be eventually called from drawWindow()
void scene::finalDrawWindow(EffectWindowImpl* w, int mask, QRegion region, WindowPaintData& data)
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

void scene::screenGeometryChanged(const QSize& size)
{
    if (!overlayWindow()) {
        return;
    }
    overlayWindow()->resize(size);
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

QImage* scene::qpainterRenderBuffer() const
{
    return nullptr;
}

QVector<QByteArray> scene::openGLPlatformInterfaceExtensions() const
{
    return QVector<QByteArray>{};
}

//****************************************
// window
//****************************************

uint32_t window_id{0};

window::window(Toplevel* c)
    : toplevel(c)
    , filter(image_filter_type::fast)
    , m_shadow(nullptr)
    , m_currentPixmap()
    , m_previousPixmap()
    , m_referencePixmapCounter(0)
    , disable_painting(0)
    , cached_quad_list(nullptr)
    , m_id{window_id++}
{
}

window::~window()
{
    delete m_shadow;
}

uint32_t window::id() const
{
    return m_id;
}

void window::referencePreviousPixmap()
{
    if (!m_previousPixmap.isNull() && m_previousPixmap->isDiscarded()) {
        m_referencePixmapCounter++;
    }
}

void window::unreferencePreviousPixmap()
{
    if (m_previousPixmap.isNull() || !m_previousPixmap->isDiscarded()) {
        return;
    }
    m_referencePixmapCounter--;
    if (m_referencePixmapCounter == 0) {
        m_previousPixmap.reset();
    }
}

void window::discardPixmap()
{
    if (!m_currentPixmap.isNull()) {
        if (m_currentPixmap->isValid()) {
            m_previousPixmap.reset(m_currentPixmap.take());
            m_previousPixmap->markAsDiscarded();
        } else {
            m_currentPixmap.reset();
        }
    }
}

void window::updatePixmap()
{
    if (m_currentPixmap.isNull()) {
        m_currentPixmap.reset(createWindowPixmap());
    }
    if (!m_currentPixmap->isValid()) {
        m_currentPixmap->create();
    }
}

QRegion window::decorationShape() const
{
    if (!win::decoration(toplevel)) {
        return QRegion();
    }
    return QRegion(QRect(QPoint(), toplevel->size())) - win::frame_relative_client_rect(toplevel);
}

QPoint window::bufferOffset() const
{
    return win::render_geometry(toplevel).topLeft() - toplevel->pos();
}

bool window::isVisible() const
{
    if (toplevel->isDeleted())
        return false;
    if (!toplevel->isOnCurrentDesktop())
        return false;
    if (toplevel->control) {
        return toplevel->isShown();
    }
    return true; // Unmanaged is always visible
}

bool window::isOpaque() const
{
    return toplevel->opacity() == 1.0 && !toplevel->hasAlpha();
}

bool window::isPaintingEnabled() const
{
    return !disable_painting;
}

void window::resetPaintingEnabled()
{
    disable_painting = 0;
    if (toplevel->isDeleted())
        disable_painting |= PAINT_DISABLED_BY_DELETE;
    if (static_cast<EffectsHandlerImpl*>(effects)->isDesktopRendering()) {
        if (!toplevel->isOnDesktop(
                static_cast<EffectsHandlerImpl*>(effects)->currentRenderedDesktop())) {
            disable_painting |= PAINT_DISABLED_BY_DESKTOP;
        }
    } else {
        if (!toplevel->isOnCurrentDesktop())
            disable_painting |= PAINT_DISABLED_BY_DESKTOP;
    }
    if (toplevel->control) {
        if (toplevel->control->minimized()) {
            disable_painting |= PAINT_DISABLED_BY_MINIMIZE;
        }
        if (toplevel->isHiddenInternal()) {
            disable_painting |= PAINT_DISABLED;
        }
    }
}

void window::enablePainting(int reason)
{
    disable_painting &= ~reason;
}

void window::disablePainting(int reason)
{
    disable_painting |= reason;
}

WindowQuadList window::buildQuads(bool force) const
{
    if (cached_quad_list != nullptr && !force)
        return *cached_quad_list;

    auto ret = makeContentsQuads(id());

    if (!win::frame_margins(toplevel).isNull()) {
        qreal decorationScale = 1.0;

        QRect rects[4];

        if (toplevel->control) {
            toplevel->layoutDecorationRects(rects[0], rects[1], rects[2], rects[3]);
            decorationScale = toplevel->screenScale();
        }

        auto const decoration_region = decorationShape();
        ret += makeDecorationQuads(rects, decoration_region, decorationScale);
    }

    if (m_shadow && toplevel->wantsShadowToBeRendered()) {
        ret << m_shadow->shadowQuads();
    }

    effects->buildQuads(toplevel->effectWindow(), ret);
    cached_quad_list.reset(new WindowQuadList(ret));
    return ret;
}

WindowQuadList window::makeDecorationQuads(const QRect* rects,
                                                  const QRegion& region,
                                                  qreal textureScale) const
{
    WindowQuadList list;

    const int padding = 1;

    const QPoint topSpritePosition(padding, padding);
    const QPoint bottomSpritePosition(padding,
                                      topSpritePosition.y() + rects[1].height() + 2 * padding);
    const QPoint leftSpritePosition(bottomSpritePosition.y() + rects[3].height() + 2 * padding,
                                    padding);
    const QPoint rightSpritePosition(leftSpritePosition.x() + rects[0].width() + 2 * padding,
                                     padding);

    const QPoint offsets[4] = {
        QPoint(-rects[0].x(), -rects[0].y()) + leftSpritePosition,
        QPoint(-rects[1].x(), -rects[1].y()) + topSpritePosition,
        QPoint(-rects[2].x(), -rects[2].y()) + rightSpritePosition,
        QPoint(-rects[3].x(), -rects[3].y()) + bottomSpritePosition,
    };

    const Qt::Orientation orientations[4] = {
        Qt::Vertical,   // Left
        Qt::Horizontal, // Top
        Qt::Vertical,   // Right
        Qt::Horizontal, // Bottom
    };

    for (int i = 0; i < 4; i++) {
        const QRegion intersectedRegion = (region & rects[i]);
        for (const QRect& r : intersectedRegion) {
            if (!r.isValid())
                continue;

            const bool swap = orientations[i] == Qt::Vertical;

            const int x0 = r.x();
            const int y0 = r.y();
            const int x1 = r.x() + r.width();
            const int y1 = r.y() + r.height();

            const int u0 = (x0 + offsets[i].x()) * textureScale;
            const int v0 = (y0 + offsets[i].y()) * textureScale;
            const int u1 = (x1 + offsets[i].x()) * textureScale;
            const int v1 = (y1 + offsets[i].y()) * textureScale;

            WindowQuad quad(WindowQuadDecoration);
            quad.setUVAxisSwapped(swap);

            if (swap) {
                quad[0] = WindowVertex(x0, y0, v0, u0); // Top-left
                quad[1] = WindowVertex(x1, y0, v0, u1); // Top-right
                quad[2] = WindowVertex(x1, y1, v1, u1); // Bottom-right
                quad[3] = WindowVertex(x0, y1, v1, u0); // Bottom-left
            } else {
                quad[0] = WindowVertex(x0, y0, u0, v0); // Top-left
                quad[1] = WindowVertex(x1, y0, u1, v0); // Top-right
                quad[2] = WindowVertex(x1, y1, u1, v1); // Bottom-right
                quad[3] = WindowVertex(x0, y1, u0, v1); // Bottom-left
            }

            list.append(quad);
        }
    }

    return list;
}

WindowQuadList window::makeContentsQuads(int id, QPoint const& offset) const
{
    auto const contentsRegion = win::content_render_region(toplevel);
    if (contentsRegion.isEmpty()) {
        return WindowQuadList();
    }

    auto const geometryOffset = offset + bufferOffset();
    const qreal textureScale = toplevel->bufferScale();

    WindowQuadList quads;
    quads.reserve(contentsRegion.rectCount());

    auto createQuad = [id, geometryOffset](QRectF const& rect, QRectF const& sourceRect) {
        WindowQuad quad(WindowQuadContents, id);

        const qreal x0 = rect.left() + geometryOffset.x();
        const qreal y0 = rect.top() + geometryOffset.y();
        const qreal x1 = rect.right() + geometryOffset.x();
        const qreal y1 = rect.bottom() + geometryOffset.y();

        const qreal u0 = sourceRect.left();
        const qreal v0 = sourceRect.top();
        const qreal u1 = sourceRect.right();
        const qreal v1 = sourceRect.bottom();

        quad[0] = WindowVertex(QPointF(x0, y0), QPointF(u0, v0));
        quad[1] = WindowVertex(QPointF(x1, y0), QPointF(u1, v0));
        quad[2] = WindowVertex(QPointF(x1, y1), QPointF(u1, v1));
        quad[3] = WindowVertex(QPointF(x0, y1), QPointF(u0, v1));
        return quad;
    };

    // Check for viewport being set. We only allow specifying the viewport at the moment for
    // non-shape windows.
    if (contentsRegion.rectCount() < 2) {
        QRectF const contentsRect = *contentsRegion.begin();
        QRectF sourceRect(contentsRect.topLeft() * textureScale,
                          contentsRect.bottomRight() * textureScale);

        if (const auto* surface = toplevel->surface()) {
            QRectF const rect = surface->state().source_rectangle;
            if (rect.isValid()) {
                sourceRect
                    = QRectF(rect.topLeft() * textureScale, rect.bottomRight() * textureScale);
            } else {
                auto buffer = surface->state().buffer;
                // XWayland client's geometry must be taken from their content placement since the
                // buffer size is not in sync.
                if (buffer && !toplevel->isClient()) {
                    // Try to get the source rectangle from the buffer size, what defines the source
                    // size without respect to destination size.
                    auto const origin = contentsRect.topLeft();
                    auto const rect
                        = QRectF(origin, buffer->size() - QSize(origin.x(), origin.y()));
                    Q_ASSERT(rect.isValid());
                    // Make sure a buffer was set already.
                    if (rect.isValid()) {
                        sourceRect = rect;
                    }
                }
            }
        }
        quads << createQuad(contentsRect, sourceRect);
    } else {
        for (QRectF const& contentsRect : contentsRegion) {
            QRectF const sourceRect(contentsRect.topLeft() * textureScale,
                                    contentsRect.bottomRight() * textureScale);
            quads << createQuad(contentsRect, sourceRect);
        }
    }

    for (auto child : toplevel->transient()->children) {
        if (!child->transient()->annexed) {
            continue;
        }
        if (child->remnant() && !toplevel->remnant()) {
            // When the child is a remnant but the parent not there is no guarentee the toplevel
            // will become one too what can cause artficats before the child cleanup timer fires.
            continue;
        }
        auto sw = win::scene_window(child);
        if (!sw) {
            continue;
        }
        if (auto const pixmap = sw->windowPixmap<window_pixmap>(); !pixmap || !pixmap->isValid()) {
            continue;
        }
        quads << sw->makeContentsQuads(sw->id(), offset + child->pos() - toplevel->pos());
    }

    return quads;
}

void window::invalidateQuadsCache()
{
    cached_quad_list.reset();
}

void window::updateShadow(Shadow* shadow)
{
    if (m_shadow == shadow) {
        return;
    }
    delete m_shadow;
    m_shadow = shadow;
}

//****************************************
// window_pixmap
//****************************************
window_pixmap::window_pixmap(render::window* window)
    : m_window(window)
    , m_pixmap(XCB_PIXMAP_NONE)
    , m_discarded(false)
{
}

window_pixmap::~window_pixmap()
{
    if (m_pixmap != XCB_WINDOW_NONE) {
        xcb_free_pixmap(connection(), m_pixmap);
    }
}

void window_pixmap::create()
{
    if (isValid() || toplevel()->isDeleted()) {
        return;
    }
    // always update from Buffer on Wayland, don't try using XPixmap
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        // use Buffer
        updateBuffer();
        if (m_buffer || !m_fbo.isNull()) {
            m_window->unreferencePreviousPixmap();
        }
        return;
    }
    XServerGrabber grabber;
    xcb_pixmap_t pix = xcb_generate_id(connection());
    xcb_void_cookie_t namePixmapCookie
        = xcb_composite_name_window_pixmap_checked(connection(), toplevel()->frameId(), pix);
    Xcb::WindowAttributes windowAttributes(toplevel()->frameId());

    auto win = toplevel();
    auto xcb_frame_geometry = Xcb::WindowGeometry(win->frameId());

    if (xcb_generic_error_t* error = xcb_request_check(connection(), namePixmapCookie)) {
        qCDebug(KWIN_CORE) << "Creating window pixmap failed: " << error->error_code;
        free(error);
        return;
    }
    // check that the received pixmap is valid and actually matches what we
    // know about the window (i.e. size)
    if (!windowAttributes || windowAttributes->map_state != XCB_MAP_STATE_VIEWABLE) {
        qCDebug(KWIN_CORE) << "Creating window pixmap failed by mapping state: " << win;
        xcb_free_pixmap(connection(), pix);
        return;
    }

    auto const render_geo = win::render_geometry(win);
    if (xcb_frame_geometry.size() != render_geo.size()) {
        qCDebug(KWIN_CORE) << "Creating window pixmap failed by size: " << win << " : "
                           << xcb_frame_geometry.rect() << " | " << render_geo;
        xcb_free_pixmap(connection(), pix);
        return;
    }

    m_pixmap = pix;
    m_pixmapSize = render_geo.size();

    // Content relative to render geometry.
    m_contentsRect = (render_geo - win::frame_margins(win)).translated(-render_geo.topLeft());

    m_window->unreferencePreviousPixmap();
}

bool window_pixmap::isValid() const
{
    if (m_buffer || !m_fbo.isNull() || !m_internalImage.isNull()) {
        return true;
    }
    return m_pixmap != XCB_PIXMAP_NONE;
}

void window_pixmap::updateBuffer()
{
    using namespace Wrapland::Server;
    if (auto s = surface()) {
        if (auto b = s->state().buffer) {
            if (b == m_buffer) {
                // no change
                return;
            }
            m_buffer = b;
        }
    } else if (toplevel()->internalFramebufferObject()) {
        m_fbo = toplevel()->internalFramebufferObject();
    } else if (!toplevel()->internalImageObject().isNull()) {
        m_internalImage = toplevel()->internalImageObject();
    } else {
        m_buffer.reset();
    }
}

Wrapland::Server::Surface* window_pixmap::surface() const
{
    return toplevel()->surface();
}

//****************************************
// effect_frame
//****************************************
effect_frame::effect_frame(EffectFrameImpl* frame)
    : m_effectFrame(frame)
{
}

effect_frame::~effect_frame()
{
}

scene_factory::scene_factory(QObject* parent)
    : QObject(parent)
{
}

scene_factory::~scene_factory()
{
}

}
