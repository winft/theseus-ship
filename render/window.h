/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"
#include "deco_shadow.h"
#include "effect/window_impl.h"
#include "shadow.h"
#include "types.h"

#include "win/desktop_get.h"
#include "win/geo.h"

#include <kwineffects/paint_data.h>

#include <functional>
#include <memory>

namespace KWin::render
{

template<typename Win>
struct window_win_integration {
    std::function<void(buffer<Win>&)> setup_buffer;
    std::function<QRectF(typename Win::ref_t*, QRectF const&)> get_viewport;
};

template<typename RefWin>
class window
{
public:
    using ref_t = RefWin;
    using type = window<ref_t>;
    using effect_window_t = effects_window_impl<type>;
    using scene_t = typename ref_t::space_t::base_t::render_t::compositor_t::scene_t;

    window(RefWin* ref_win)
        : ref_win{ref_win}
        , scene{*ref_win->space.base.render->compositor->scene}
        , filter(image_filter_type::fast)
        , cached_quad_list(nullptr)
        , m_id{scene.window_id++}
    {
    }

    virtual ~window() = default;

    uint32_t id() const
    {
        return m_id;
    }

    // perform the actual painting of the window
    virtual void performPaint(paint_type mask, QRegion region, WindowPaintData data) = 0;

    // do any cleanup needed when the window's buffer is discarded
    void discard_buffer()
    {
        if (!buffers.current) {
            return;
        }

        if (!buffers.current->isValid()) {
            // An invalid buffer is simply being reset.
            buffers.current.reset();
            return;
        }

        // Move the current buffer to previous buffer.
        buffers.previous = std::move(buffers.current);
        buffers.previous->markAsDiscarded();
    }

    void update_buffer()
    {
        if (!buffers.current) {
            buffers.current.reset(create_buffer());
            assert(win_integration.setup_buffer);
            win_integration.setup_buffer(*buffers.current);
        }
        if (!buffers.current->isValid()) {
            buffers.current->create();
        }
    }

    // should the window be painted
    bool isPaintingEnabled() const
    {
        return disable_painting == window_paint_disable_type::none;
    }

    void resetPaintingEnabled()
    {
        disable_painting = window_paint_disable_type::none;
        if (ref_win->remnant) {
            disable_painting |= window_paint_disable_type::by_delete;
        }
        if (scene.platform.compositor->effects->isDesktopRendering()) {
            if (!win::on_desktop(ref_win,
                                 scene.platform.compositor->effects->currentRenderedDesktop())) {
                disable_painting |= window_paint_disable_type::by_desktop;
            }
        } else {
            if (!win::on_current_desktop(ref_win))
                disable_painting |= window_paint_disable_type::by_desktop;
        }
        if (ref_win->control) {
            if (ref_win->control->minimized) {
                disable_painting |= window_paint_disable_type::by_minimize;
            }
            if (ref_win->isHiddenInternal()) {
                disable_painting |= window_paint_disable_type::unspecified;
            }
        }
    }

    void enablePainting(window_paint_disable_type reason)
    {
        disable_painting &= ~reason;
    }

    void disablePainting(window_paint_disable_type reason)
    {
        disable_painting |= reason;
    }

    // is the window visible at all
    bool isVisible() const
    {
        if (ref_win->remnant)
            return false;
        if (!win::on_current_desktop(ref_win))
            return false;
        if (ref_win->control) {
            return ref_win->isShown();
        }
        return true; // Unmanaged is always visible
    }

    // is the window fully opaque
    bool isOpaque() const
    {
        return ref_win->opacity() == 1.0 && !ref_win->hasAlpha();
    }

    QRegion decorationShape() const
    {
        if (!win::decoration(ref_win)) {
            return QRegion();
        }
        return QRegion(QRect(QPoint(), ref_win->size())) - win::frame_relative_client_rect(ref_win);
    }

    QPoint bufferOffset() const
    {
        return win::render_geometry(ref_win).topLeft() - ref_win->pos();
    }

    // creates initial quad list for the window
    WindowQuadList buildQuads(bool force = false) const
    {
        if (cached_quad_list != nullptr && !force) {
            return *cached_quad_list;
        }

        auto ret = makeContentsQuads(id());

        if (!win::frame_margins(ref_win).isNull()) {
            qreal decorationScale = 1.0;

            QRect rects[4];

            if (ref_win->control) {
                ref_win->layoutDecorationRects(rects[0], rects[1], rects[2], rects[3]);
                decorationScale = ref_win->central_output ? ref_win->central_output->scale() : 1.;
            }

            auto const decoration_region = decorationShape();
            ret += makeDecorationQuads(rects, decoration_region, decorationScale);
        }

        if (m_shadow && ref_win->wantsShadowToBeRendered()) {
            ret << m_shadow->shadowQuads();
        }

        scene.platform.compositor->effects->buildQuads(effect.get(), ret);
        cached_quad_list.reset(new WindowQuadList(ret));
        return ret;
    }

    void create_shadow()
    {
        auto shadow = create_deco_shadow<render::shadow<type>>(*ref_win);

        if (!shadow && shadow_windowing.create) {
            shadow = shadow_windowing.create(*this);
        }

        if (shadow) {
            updateShadow(std::move(shadow));
            Q_EMIT ref_win->qobject->shadowChanged();
        }
    }

    void updateShadow(std::unique_ptr<render::shadow<type>> shadow)
    {
        m_shadow = std::move(shadow);
    }

    render::shadow<type> const* shadow() const
    {
        return m_shadow.get();
    }

    render::shadow<type>* shadow()
    {
        return m_shadow.get();
    }

    void reference_previous_buffer()
    {
        if (buffers.previous && buffers.previous->isDiscarded()) {
            buffers.previous_refs++;
        }
    }

    void unreference_previous_buffer()
    {
        if (!buffers.previous || !buffers.previous->isDiscarded()) {
            return;
        }
        buffers.previous_refs--;
        assert(buffers.previous_refs >= 0);
        if (buffers.previous_refs == 0) {
            buffers.previous.reset();
        }
    }

    void invalidateQuadsCache()
    {
        cached_quad_list.reset();
    }

    RefWin* ref_win;

    std::unique_ptr<effects_window_impl<type>> effect;
    window_win_integration<type> win_integration;
    shadow_windowing_integration<type> shadow_windowing;
    scene_t& scene;

protected:
    WindowQuadList
    makeDecorationQuads(const QRect* rects, const QRegion& region, qreal textureScale = 1.0) const
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

    WindowQuadList makeContentsQuads(int id, QPoint const& offset = QPoint()) const
    {
        auto const contentsRegion = win::content_render_region(ref_win);
        if (contentsRegion.isEmpty()) {
            return WindowQuadList();
        }

        auto const geometryOffset = offset + bufferOffset();
        const qreal textureScale = ref_win->bufferScale();

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
            if (auto& vp_getter = win_integration.get_viewport) {
                if (auto vp = vp_getter(ref_win, contentsRect); vp.isValid()) {
                    sourceRect = vp;
                }
            }
            quads << createQuad(contentsRect, sourceRect);
        } else {
            for (auto const& contentsRect : contentsRegion) {
                auto const sourceRect = QRectF(contentsRect.topLeft() * textureScale,
                                               contentsRect.bottomRight() * textureScale);
                quads << createQuad(contentsRect, sourceRect);
            }
        }

        for (auto child : ref_win->transient()->children) {
            if (!child->transient()->annexed) {
                continue;
            }
            if (child->remnant && !ref_win->remnant) {
                // When the child is a remnant but the parent not there is no guarentee the ref_win
                // will become one too what can cause artficats before the child cleanup timer
                // fires.
                continue;
            }
            auto& sw = child->render;
            if (!sw) {
                continue;
            }

            using buffer_t = buffer<type>;
            if (auto const buf = sw->template get_buffer<buffer_t>(); !buf || !buf->isValid()) {
                continue;
            }
            quads << sw->makeContentsQuads(sw->id(), offset + child->pos() - ref_win->pos());
        }

        return quads;
    }

    /**
     * @brief Returns the buffer for this Window.
     *
     * If the buffer does not yet exist, this method will invoke create_buffer.
     * If the buffer is not valid it tries to create it, in case this succeeds the
     * buffer is returned. In case it fails, the previous (and still valid) buffer is
     * returned.
     *
     * @note This method can return @c NULL as there might neither be a valid previous nor current
     * buffer around.
     *
     * The buffer gets casted to the type passed in as a template parameter. That way this
     * class does not need to know the actual buffer subclass used by the concrete scene
     * implementations.
     *
     * @return The buffer casted to T* or @c NULL if there is no valid buffer.
     */
    template<typename T>
    T* get_buffer()
    {
        update_buffer();
        if (buffers.current->isValid()) {
            return static_cast<T*>(buffers.current.get());
        }
        return static_cast<T*>(buffers.previous.get());
    }

    template<typename T>
    T* previous_buffer()
    {
        return static_cast<T*>(buffers.previous.get());
    }

    /**
     * @brief Factory method to create a buffer.
     *
     * The inheriting classes need to implement this method to create a new instance of their
     * buffer subclass.
     * @note Do not use buffer::create on the created instance. The scene will take care of
     * that.
     */
    virtual buffer<type>* create_buffer() = 0;

    image_filter_type filter;
    std::unique_ptr<render::shadow<type>> m_shadow;

private:
    struct {
        std::unique_ptr<buffer<type>> current;
        std::unique_ptr<buffer<type>> previous;
        int previous_refs{0};
    } buffers;
    window_paint_disable_type disable_painting{window_paint_disable_type::none};
    mutable std::unique_ptr<WindowQuadList> cached_quad_list;
    uint32_t const m_id;
};

}
