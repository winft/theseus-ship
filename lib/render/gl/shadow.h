/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/shadow.h"

#include <render/gl/interface/platform.h>
#include <render/gl/interface/utils.h>

#include <QPainter>
#include <QSharedPointer>

namespace KWin::render::gl
{

template<typename Shadow>
class DecorationShadowTextureCache
{
public:
    ~DecorationShadowTextureCache()
    {
        assert(m_cache.isEmpty());
    }

    DecorationShadowTextureCache(const DecorationShadowTextureCache&) = delete;

    static DecorationShadowTextureCache& instance()
    {
        static DecorationShadowTextureCache s_instance;
        return s_instance;
    }

    void unregister(Shadow* shadow)
    {
        auto it = m_cache.begin();
        while (it != m_cache.end()) {
            auto& d = it.value();
            // check whether the Vector of Shadows contains our shadow and remove all of them
            auto glIt = d.shadows.begin();
            while (glIt != d.shadows.end()) {
                if (*glIt == shadow) {
                    glIt = d.shadows.erase(glIt);
                } else {
                    glIt++;
                }
            }
            // if there are no shadows any more we can erase the cache entry
            if (d.shadows.isEmpty()) {
                it = m_cache.erase(it);
            } else {
                it++;
            }
        }
    }

    QSharedPointer<GLTexture> getTexture(Shadow* shadow)
    {
        Q_ASSERT(shadow->hasDecorationShadow());
        unregister(shadow);
        auto const decoShadow = shadow->decorationShadow().lock();
        Q_ASSERT(decoShadow);
        auto it = m_cache.find(decoShadow.get());
        if (it != m_cache.end()) {
            Q_ASSERT(!it.value().shadows.contains(shadow));
            it.value().shadows << shadow;
            return it.value().texture;
        }
        Data d;
        d.shadows << shadow;
        d.texture = QSharedPointer<GLTexture>::create(shadow->decorationShadowImage());
        m_cache.insert(decoShadow.get(), d);
        return d.texture;
    }

private:
    DecorationShadowTextureCache() = default;
    struct Data {
        QSharedPointer<GLTexture> texture;
        QVector<Shadow*> shadows;
    };
    QHash<KDecoration2::DecorationShadow*, Data> m_cache;
};

template<typename Window, typename Scene>
class shadow : public render::shadow<Window>
{
public:
    using type = shadow<Window, Scene>;

    shadow(Window* window, Scene& scene)
        : render::shadow<Window>(window)
        , scene{scene}
    {
    }

    ~shadow() override
    {
        scene.makeOpenGLContextCurrent();
        DecorationShadowTextureCache<type>::instance().unregister(this);
        m_texture.reset();
    }

    GLTexture* shadowTexture()
    {
        return m_texture.data();
    }

protected:
    void buildQuads() override
    {
        auto const& window_size = std::visit(overload{[](auto&& win) { return win->geo.size(); }},
                                             *this->window->ref_win);

        // Do not draw shadows if window width or window height is less than
        // 5 px. 5 is an arbitrary choice.
        if (window_size.width() < 5 || window_size.height() < 5) {
            this->m_shadowQuads.clear();
            this->setShadowRegion(QRegion());
            return;
        }

        const QSizeF top(this->elementSize(shadow_element::top));
        const QSizeF topRight(this->elementSize(shadow_element::top_right));
        const QSizeF right(this->elementSize(shadow_element::right));
        const QSizeF bottomRight(this->elementSize(shadow_element::bottom_right));
        const QSizeF bottom(this->elementSize(shadow_element::bottom));
        const QSizeF bottomLeft(this->elementSize(shadow_element::bottom_left));
        const QSizeF left(this->elementSize(shadow_element::left));
        const QSizeF topLeft(this->elementSize(shadow_element::top_left));

        const QMarginsF shadowMargins(
            std::max({topLeft.width(), left.width(), bottomLeft.width()}),
            std::max({topLeft.height(), top.height(), topRight.height()}),
            std::max({topRight.width(), right.width(), bottomRight.width()}),
            std::max({bottomRight.height(), bottom.height(), bottomLeft.height()}));

        const QRectF outerRect(QPointF(-this->leftOffset(), -this->topOffset()),
                               QPointF(window_size.width() + this->rightOffset(),
                                       window_size.height() + this->bottomOffset()));

        const int width
            = shadowMargins.left() + std::max(top.width(), bottom.width()) + shadowMargins.right();
        const int height = shadowMargins.top() + std::max(left.height(), right.height())
            + shadowMargins.bottom();

        QRectF topLeftRect;
        if (!topLeft.isEmpty()) {
            topLeftRect = QRectF(outerRect.topLeft(), topLeft);
        } else {
            topLeftRect = QRectF(outerRect.left() + shadowMargins.left(),
                                 outerRect.top() + shadowMargins.top(),
                                 0,
                                 0);
        }

        QRectF topRightRect;
        if (!topRight.isEmpty()) {
            topRightRect = QRectF(outerRect.right() - topRight.width(),
                                  outerRect.top(),
                                  topRight.width(),
                                  topRight.height());
        } else {
            topRightRect = QRectF(outerRect.right() - shadowMargins.right(),
                                  outerRect.top() + shadowMargins.top(),
                                  0,
                                  0);
        }

        QRectF bottomRightRect;
        if (!bottomRight.isEmpty()) {
            bottomRightRect = QRectF(outerRect.right() - bottomRight.width(),
                                     outerRect.bottom() - bottomRight.height(),
                                     bottomRight.width(),
                                     bottomRight.height());
        } else {
            bottomRightRect = QRectF(outerRect.right() - shadowMargins.right(),
                                     outerRect.bottom() - shadowMargins.bottom(),
                                     0,
                                     0);
        }

        QRectF bottomLeftRect;
        if (!bottomLeft.isEmpty()) {
            bottomLeftRect = QRectF(outerRect.left(),
                                    outerRect.bottom() - bottomLeft.height(),
                                    bottomLeft.width(),
                                    bottomLeft.height());
        } else {
            bottomLeftRect = QRectF(outerRect.left() + shadowMargins.left(),
                                    outerRect.bottom() - shadowMargins.bottom(),
                                    0,
                                    0);
        }

        // Re-distribute the corner tiles so no one of them is overlapping with others.
        // By doing this, we assume that shadow's corner tiles are symmetric
        // and it is OK to not draw top/right/bottom/left tile between corners.
        // For example, let's say top-left and top-right tiles are overlapping.
        // In that case, the right side of the top-left tile will be shifted to left,
        // the left side of the top-right tile will shifted to right, and the top
        // tile won't be rendered.
        distributeHorizontally(topLeftRect, topRightRect);
        distributeHorizontally(bottomLeftRect, bottomRightRect);
        distributeVertically(topLeftRect, bottomLeftRect);
        distributeVertically(topRightRect, bottomRightRect);

        qreal tx1 = 0.0, tx2 = 0.0, ty1 = 0.0, ty2 = 0.0;

        this->m_shadowQuads.clear();

        if (topLeftRect.isValid()) {
            tx1 = 0.0;
            ty1 = 0.0;
            tx2 = topLeftRect.width() / width;
            ty2 = topLeftRect.height() / height;
            WindowQuad topLeftQuad(WindowQuadShadow);
            topLeftQuad[0] = WindowVertex(topLeftRect.left(), topLeftRect.top(), tx1, ty1);
            topLeftQuad[1] = WindowVertex(topLeftRect.right(), topLeftRect.top(), tx2, ty1);
            topLeftQuad[2] = WindowVertex(topLeftRect.right(), topLeftRect.bottom(), tx2, ty2);
            topLeftQuad[3] = WindowVertex(topLeftRect.left(), topLeftRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(topLeftQuad);
        }

        if (topRightRect.isValid()) {
            tx1 = 1.0 - topRightRect.width() / width;
            ty1 = 0.0;
            tx2 = 1.0;
            ty2 = topRightRect.height() / height;
            WindowQuad topRightQuad(WindowQuadShadow);
            topRightQuad[0] = WindowVertex(topRightRect.left(), topRightRect.top(), tx1, ty1);
            topRightQuad[1] = WindowVertex(topRightRect.right(), topRightRect.top(), tx2, ty1);
            topRightQuad[2] = WindowVertex(topRightRect.right(), topRightRect.bottom(), tx2, ty2);
            topRightQuad[3] = WindowVertex(topRightRect.left(), topRightRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(topRightQuad);
        }

        if (bottomRightRect.isValid()) {
            tx1 = 1.0 - bottomRightRect.width() / width;
            tx2 = 1.0;
            ty1 = 1.0 - bottomRightRect.height() / height;
            ty2 = 1.0;
            WindowQuad bottomRightQuad(WindowQuadShadow);
            bottomRightQuad[0]
                = WindowVertex(bottomRightRect.left(), bottomRightRect.top(), tx1, ty1);
            bottomRightQuad[1]
                = WindowVertex(bottomRightRect.right(), bottomRightRect.top(), tx2, ty1);
            bottomRightQuad[2]
                = WindowVertex(bottomRightRect.right(), bottomRightRect.bottom(), tx2, ty2);
            bottomRightQuad[3]
                = WindowVertex(bottomRightRect.left(), bottomRightRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(bottomRightQuad);
        }

        if (bottomLeftRect.isValid()) {
            tx1 = 0.0;
            tx2 = bottomLeftRect.width() / width;
            ty1 = 1.0 - bottomLeftRect.height() / height;
            ty2 = 1.0;
            WindowQuad bottomLeftQuad(WindowQuadShadow);
            bottomLeftQuad[0] = WindowVertex(bottomLeftRect.left(), bottomLeftRect.top(), tx1, ty1);
            bottomLeftQuad[1]
                = WindowVertex(bottomLeftRect.right(), bottomLeftRect.top(), tx2, ty1);
            bottomLeftQuad[2]
                = WindowVertex(bottomLeftRect.right(), bottomLeftRect.bottom(), tx2, ty2);
            bottomLeftQuad[3]
                = WindowVertex(bottomLeftRect.left(), bottomLeftRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(bottomLeftQuad);
        }

        QRectF topRect(QPointF(topLeftRect.right(), outerRect.top()),
                       QPointF(topRightRect.left(), outerRect.top() + top.height()));

        QRectF rightRect(QPointF(outerRect.right() - right.width(), topRightRect.bottom()),
                         QPointF(outerRect.right(), bottomRightRect.top()));

        QRectF bottomRect(QPointF(bottomLeftRect.right(), outerRect.bottom() - bottom.height()),
                          QPointF(bottomRightRect.left(), outerRect.bottom()));

        QRectF leftRect(QPointF(outerRect.left(), topLeftRect.bottom()),
                        QPointF(outerRect.left() + left.width(), bottomLeftRect.top()));

        // Re-distribute left/right and top/bottom shadow tiles so they don't
        // overlap when the window is too small. Please notice that we don't
        // fix overlaps between left/top(left/bottom, right/top, and so on)
        // corner tiles because corresponding counter parts won't be valid when
        // the window is too small, which means they won't be rendered.
        distributeHorizontally(leftRect, rightRect);
        distributeVertically(topRect, bottomRect);

        if (topRect.isValid()) {
            tx1 = shadowMargins.left() / width;
            ty1 = 0.0;
            tx2 = tx1 + top.width() / width;
            ty2 = topRect.height() / height;
            WindowQuad topQuad(WindowQuadShadow);
            topQuad[0] = WindowVertex(topRect.left(), topRect.top(), tx1, ty1);
            topQuad[1] = WindowVertex(topRect.right(), topRect.top(), tx2, ty1);
            topQuad[2] = WindowVertex(topRect.right(), topRect.bottom(), tx2, ty2);
            topQuad[3] = WindowVertex(topRect.left(), topRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(topQuad);
        }

        if (rightRect.isValid()) {
            tx1 = 1.0 - rightRect.width() / width;
            ty1 = shadowMargins.top() / height;
            tx2 = 1.0;
            ty2 = ty1 + right.height() / height;
            WindowQuad rightQuad(WindowQuadShadow);
            rightQuad[0] = WindowVertex(rightRect.left(), rightRect.top(), tx1, ty1);
            rightQuad[1] = WindowVertex(rightRect.right(), rightRect.top(), tx2, ty1);
            rightQuad[2] = WindowVertex(rightRect.right(), rightRect.bottom(), tx2, ty2);
            rightQuad[3] = WindowVertex(rightRect.left(), rightRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(rightQuad);
        }

        if (bottomRect.isValid()) {
            tx1 = shadowMargins.left() / width;
            ty1 = 1.0 - bottomRect.height() / height;
            tx2 = tx1 + bottom.width() / width;
            ty2 = 1.0;
            WindowQuad bottomQuad(WindowQuadShadow);
            bottomQuad[0] = WindowVertex(bottomRect.left(), bottomRect.top(), tx1, ty1);
            bottomQuad[1] = WindowVertex(bottomRect.right(), bottomRect.top(), tx2, ty1);
            bottomQuad[2] = WindowVertex(bottomRect.right(), bottomRect.bottom(), tx2, ty2);
            bottomQuad[3] = WindowVertex(bottomRect.left(), bottomRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(bottomQuad);
        }

        if (leftRect.isValid()) {
            tx1 = 0.0;
            ty1 = shadowMargins.top() / height;
            tx2 = leftRect.width() / width;
            ty2 = ty1 + left.height() / height;
            WindowQuad leftQuad(WindowQuadShadow);
            leftQuad[0] = WindowVertex(leftRect.left(), leftRect.top(), tx1, ty1);
            leftQuad[1] = WindowVertex(leftRect.right(), leftRect.top(), tx2, ty1);
            leftQuad[2] = WindowVertex(leftRect.right(), leftRect.bottom(), tx2, ty2);
            leftQuad[3] = WindowVertex(leftRect.left(), leftRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(leftQuad);
        }
    }

    bool prepareBackend() override
    {
        if (this->hasDecorationShadow()) {
            // simplifies a lot by going directly to
            scene.makeOpenGLContextCurrent();
            m_texture = DecorationShadowTextureCache<type>::instance().getTexture(this);

            return true;
        }
        const QSize top(this->shadowPixmap(shadow_element::top).size());
        const QSize topRight(this->shadowPixmap(shadow_element::top_right).size());
        const QSize right(this->shadowPixmap(shadow_element::right).size());
        const QSize bottom(this->shadowPixmap(shadow_element::bottom).size());
        const QSize bottomLeft(this->shadowPixmap(shadow_element::bottom_left).size());
        const QSize left(this->shadowPixmap(shadow_element::left).size());
        const QSize topLeft(this->shadowPixmap(shadow_element::top_left).size());
        const QSize bottomRight(this->shadowPixmap(shadow_element::bottom_right).size());

        const int width = std::max({topLeft.width(), left.width(), bottomLeft.width()})
            + std::max(top.width(), bottom.width())
            + std::max({topRight.width(), right.width(), bottomRight.width()});
        const int height = std::max({topLeft.height(), top.height(), topRight.height()})
            + std::max(left.height(), right.height())
            + std::max({bottomLeft.height(), bottom.height(), bottomRight.height()});

        if (width == 0 || height == 0) {
            return false;
        }

        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(Qt::transparent);

        const int innerRectTop = std::max({topLeft.height(), top.height(), topRight.height()});
        const int innerRectLeft = std::max({topLeft.width(), left.width(), bottomLeft.width()});

        QPainter p;
        p.begin(&image);

        p.drawPixmap(
            0, 0, topLeft.width(), topLeft.height(), this->shadowPixmap(shadow_element::top_left));
        p.drawPixmap(
            innerRectLeft, 0, top.width(), top.height(), this->shadowPixmap(shadow_element::top));
        p.drawPixmap(width - topRight.width(),
                     0,
                     topRight.width(),
                     topRight.height(),
                     this->shadowPixmap(shadow_element::top_right));

        p.drawPixmap(
            0, innerRectTop, left.width(), left.height(), this->shadowPixmap(shadow_element::left));
        p.drawPixmap(width - right.width(),
                     innerRectTop,
                     right.width(),
                     right.height(),
                     this->shadowPixmap(shadow_element::right));

        p.drawPixmap(0,
                     height - bottomLeft.height(),
                     bottomLeft.width(),
                     bottomLeft.height(),
                     this->shadowPixmap(shadow_element::bottom_left));
        p.drawPixmap(innerRectLeft,
                     height - bottom.height(),
                     bottom.width(),
                     bottom.height(),
                     this->shadowPixmap(shadow_element::bottom));
        p.drawPixmap(width - bottomRight.width(),
                     height - bottomRight.height(),
                     bottomRight.width(),
                     bottomRight.height(),
                     this->shadowPixmap(shadow_element::bottom_right));

        p.end();

        // Check if the image is alpha-only in practice, and if so convert it to an 8-bpp format
        if (!GLPlatform::instance()->isGLES() && GLTexture::supportsSwizzle()
            && GLTexture::supportsFormatRG()) {
            QImage alphaImage(image.size(), QImage::Format_Alpha8);
            bool alphaOnly = true;

            for (ptrdiff_t y = 0; alphaOnly && y < image.height(); y++) {
                const uint32_t* const src = reinterpret_cast<const uint32_t*>(image.scanLine(y));
                uint8_t* const dst = reinterpret_cast<uint8_t*>(alphaImage.scanLine(y));

                for (ptrdiff_t x = 0; x < image.width(); x++) {
                    if (src[x] & 0x00ffffff)
                        alphaOnly = false;

                    dst[x] = qAlpha(src[x]);
                }
            }

            if (alphaOnly) {
                image = alphaImage;
            }
        }

        scene.makeOpenGLContextCurrent();
        m_texture = QSharedPointer<GLTexture>::create(image);

        if (m_texture->internalFormat() == GL_R8) {
            // Swizzle red to alpha and all other channels to zero
            m_texture->bind();
            m_texture->setSwizzle(GL_ZERO, GL_ZERO, GL_ZERO, GL_RED);
        }

        return true;
    }

private:
    static inline void distributeHorizontally(QRectF& leftRect, QRectF& rightRect)
    {
        if (leftRect.right() > rightRect.left()) {
            const qreal boundedRight = qMin(leftRect.right(), rightRect.right());
            const qreal boundedLeft = qMax(leftRect.left(), rightRect.left());
            const qreal halfOverlap = (boundedRight - boundedLeft) / 2.0;
            leftRect.setRight(boundedRight - halfOverlap);
            rightRect.setLeft(boundedLeft + halfOverlap);
        }
    }

    static inline void distributeVertically(QRectF& topRect, QRectF& bottomRect)
    {
        if (topRect.bottom() > bottomRect.top()) {
            const qreal boundedBottom = qMin(topRect.bottom(), bottomRect.bottom());
            const qreal boundedTop = qMax(topRect.top(), bottomRect.top());
            const qreal halfOverlap = (boundedBottom - boundedTop) / 2.0;
            topRect.setBottom(boundedBottom - halfOverlap);
            bottomRect.setTop(boundedTop + halfOverlap);
        }
    }

    QSharedPointer<GLTexture> m_texture;
    Scene& scene;
};

}
