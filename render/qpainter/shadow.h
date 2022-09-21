/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/shadow.h"

#include <QPainter>

namespace KWin::render::qpainter
{

template<typename Window>
class shadow : public render::shadow<Window>
{
public:
    shadow(Window* window)
        : render::shadow<Window>(window)
    {
    }

    QImage& shadowTexture()
    {
        return m_texture;
    }

protected:
    void buildQuads() override
    {
        auto const& window_size = std::visit(overload{[&](auto&& win) { return win->geo.size(); }},
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

        const QRectF outerRect(QPointF(-this->leftOffset(), -this->topOffset()),
                               QPointF(window_size.width() + this->rightOffset(),
                                       window_size.height() + this->bottomOffset()));

        const int width = std::max({topLeft.width(), left.width(), bottomLeft.width()})
            + std::max(top.width(), bottom.width())
            + std::max({topRight.width(), right.width(), bottomRight.width()});
        const int height = std::max({topLeft.height(), top.height(), topRight.height()})
            + std::max(left.height(), right.height())
            + std::max({bottomLeft.height(), bottom.height(), bottomRight.height()});

        QRectF topLeftRect(outerRect.topLeft(), topLeft);
        QRectF topRightRect(outerRect.topRight() - QPointF(topRight.width(), 0), topRight);
        QRectF bottomRightRect(outerRect.bottomRight()
                                   - QPointF(bottomRight.width(), bottomRight.height()),
                               bottomRight);
        QRectF bottomLeftRect(outerRect.bottomLeft() - QPointF(0, bottomLeft.height()), bottomLeft);

        // Re-distribute the corner tiles so no one of them is overlapping with others.
        // By doing this, we assume that shadow's corner tiles are symmetric
        // and it is OK to not draw top/right/bottom/left tile between corners.
        // For example, let's say top-left and top-right tiles are overlapping.
        // In that case, the right side of the top-left tile will be shifted to left,
        // the left side of the top-right tile will shifted to right, and the top
        // tile won't be rendered.
        bool drawTop = true;
        if (topLeftRect.right() >= topRightRect.left()) {
            const float halfOverlap = qAbs(topLeftRect.right() - topRightRect.left()) / 2;
            topLeftRect.setRight(topLeftRect.right() - std::floor(halfOverlap));
            topRightRect.setLeft(topRightRect.left() + std::ceil(halfOverlap));
            drawTop = false;
        }

        bool drawRight = true;
        if (topRightRect.bottom() >= bottomRightRect.top()) {
            const float halfOverlap = qAbs(topRightRect.bottom() - bottomRightRect.top()) / 2;
            topRightRect.setBottom(topRightRect.bottom() - std::floor(halfOverlap));
            bottomRightRect.setTop(bottomRightRect.top() + std::ceil(halfOverlap));
            drawRight = false;
        }

        bool drawBottom = true;
        if (bottomLeftRect.right() >= bottomRightRect.left()) {
            const float halfOverlap = qAbs(bottomLeftRect.right() - bottomRightRect.left()) / 2;
            bottomLeftRect.setRight(bottomLeftRect.right() - std::floor(halfOverlap));
            bottomRightRect.setLeft(bottomRightRect.left() + std::ceil(halfOverlap));
            drawBottom = false;
        }

        bool drawLeft = true;
        if (topLeftRect.bottom() >= bottomLeftRect.top()) {
            const float halfOverlap = qAbs(topLeftRect.bottom() - bottomLeftRect.top()) / 2;
            topLeftRect.setBottom(topLeftRect.bottom() - std::floor(halfOverlap));
            bottomLeftRect.setTop(bottomLeftRect.top() + std::ceil(halfOverlap));
            drawLeft = false;
        }

        qreal tx1 = 0.0, tx2 = 0.0, ty1 = 0.0, ty2 = 0.0;

        this->m_shadowQuads.clear();

        tx1 = 0.0;
        ty1 = 0.0;
        tx2 = topLeftRect.width();
        ty2 = topLeftRect.height();
        WindowQuad topLeftQuad(WindowQuadShadow);
        topLeftQuad[0] = WindowVertex(topLeftRect.left(), topLeftRect.top(), tx1, ty1);
        topLeftQuad[1] = WindowVertex(topLeftRect.right(), topLeftRect.top(), tx2, ty1);
        topLeftQuad[2] = WindowVertex(topLeftRect.right(), topLeftRect.bottom(), tx2, ty2);
        topLeftQuad[3] = WindowVertex(topLeftRect.left(), topLeftRect.bottom(), tx1, ty2);
        this->m_shadowQuads.append(topLeftQuad);

        tx1 = width - topRightRect.width();
        ty1 = 0.0;
        tx2 = width;
        ty2 = topRightRect.height();
        WindowQuad topRightQuad(WindowQuadShadow);
        topRightQuad[0] = WindowVertex(topRightRect.left(), topRightRect.top(), tx1, ty1);
        topRightQuad[1] = WindowVertex(topRightRect.right(), topRightRect.top(), tx2, ty1);
        topRightQuad[2] = WindowVertex(topRightRect.right(), topRightRect.bottom(), tx2, ty2);
        topRightQuad[3] = WindowVertex(topRightRect.left(), topRightRect.bottom(), tx1, ty2);
        this->m_shadowQuads.append(topRightQuad);

        tx1 = width - bottomRightRect.width();
        tx2 = width;
        ty1 = height - bottomRightRect.height();
        ty2 = height;
        WindowQuad bottomRightQuad(WindowQuadShadow);
        bottomRightQuad[0] = WindowVertex(bottomRightRect.left(), bottomRightRect.top(), tx1, ty1);
        bottomRightQuad[1] = WindowVertex(bottomRightRect.right(), bottomRightRect.top(), tx2, ty1);
        bottomRightQuad[2]
            = WindowVertex(bottomRightRect.right(), bottomRightRect.bottom(), tx2, ty2);
        bottomRightQuad[3]
            = WindowVertex(bottomRightRect.left(), bottomRightRect.bottom(), tx1, ty2);
        this->m_shadowQuads.append(bottomRightQuad);

        tx1 = 0.0;
        tx2 = bottomLeftRect.width();
        ty1 = height - bottomLeftRect.height();
        ty2 = height;
        WindowQuad bottomLeftQuad(WindowQuadShadow);
        bottomLeftQuad[0] = WindowVertex(bottomLeftRect.left(), bottomLeftRect.top(), tx1, ty1);
        bottomLeftQuad[1] = WindowVertex(bottomLeftRect.right(), bottomLeftRect.top(), tx2, ty1);
        bottomLeftQuad[2] = WindowVertex(bottomLeftRect.right(), bottomLeftRect.bottom(), tx2, ty2);
        bottomLeftQuad[3] = WindowVertex(bottomLeftRect.left(), bottomLeftRect.bottom(), tx1, ty2);
        this->m_shadowQuads.append(bottomLeftQuad);

        if (drawTop) {
            QRectF topRect(topLeftRect.topRight(), topRightRect.bottomLeft());
            tx1 = topLeft.width();
            ty1 = 0.0;
            tx2 = width - topRight.width();
            ty2 = topRect.height();
            WindowQuad topQuad(WindowQuadShadow);
            topQuad[0] = WindowVertex(topRect.left(), topRect.top(), tx1, ty1);
            topQuad[1] = WindowVertex(topRect.right(), topRect.top(), tx2, ty1);
            topQuad[2] = WindowVertex(topRect.right(), topRect.bottom(), tx2, ty2);
            topQuad[3] = WindowVertex(topRect.left(), topRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(topQuad);
        }

        if (drawRight) {
            QRectF rightRect(topRightRect.bottomLeft(), bottomRightRect.topRight());
            tx1 = width - rightRect.width();
            ty1 = topRight.height();
            tx2 = width;
            ty2 = height - bottomRight.height();
            WindowQuad rightQuad(WindowQuadShadow);
            rightQuad[0] = WindowVertex(rightRect.left(), rightRect.top(), tx1, ty1);
            rightQuad[1] = WindowVertex(rightRect.right(), rightRect.top(), tx2, ty1);
            rightQuad[2] = WindowVertex(rightRect.right(), rightRect.bottom(), tx2, ty2);
            rightQuad[3] = WindowVertex(rightRect.left(), rightRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(rightQuad);
        }

        if (drawBottom) {
            QRectF bottomRect(bottomLeftRect.topRight(), bottomRightRect.bottomLeft());
            tx1 = bottomLeft.width();
            ty1 = height - bottomRect.height();
            tx2 = width - bottomRight.width();
            ty2 = height;
            WindowQuad bottomQuad(WindowQuadShadow);
            bottomQuad[0] = WindowVertex(bottomRect.left(), bottomRect.top(), tx1, ty1);
            bottomQuad[1] = WindowVertex(bottomRect.right(), bottomRect.top(), tx2, ty1);
            bottomQuad[2] = WindowVertex(bottomRect.right(), bottomRect.bottom(), tx2, ty2);
            bottomQuad[3] = WindowVertex(bottomRect.left(), bottomRect.bottom(), tx1, ty2);
            this->m_shadowQuads.append(bottomQuad);
        }

        if (drawLeft) {
            QRectF leftRect(topLeftRect.bottomLeft(), bottomLeftRect.topRight());
            tx1 = 0.0;
            ty1 = topLeft.height();
            tx2 = leftRect.width();
            ty2 = height - bottomRight.height();
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
            m_texture = this->decorationShadowImage();
            return true;
        }

        auto& topLeft = this->shadowPixmap(shadow_element::top_left);
        auto& top = this->shadowPixmap(shadow_element::top);
        auto& topRight = this->shadowPixmap(shadow_element::top_right);
        auto& bottomLeft = this->shadowPixmap(shadow_element::bottom_left);
        auto& bottom = this->shadowPixmap(shadow_element::bottom);
        auto& bottomRight = this->shadowPixmap(shadow_element::bottom_right);
        auto& left = this->shadowPixmap(shadow_element::left);
        auto& right = this->shadowPixmap(shadow_element::right);

        const int width = std::max({topLeft.width(), left.width(), bottomLeft.width()})
            + std::max(top.width(), bottom.width())
            + std::max({topRight.width(), right.width(), bottomRight.width()});
        const int height = std::max({topLeft.height(), top.height(), topRight.height()})
            + std::max(left.height(), right.height())
            + std::max({bottomLeft.height(), bottom.height(), bottomRight.height()});

        if (width == 0 || height == 0) {
            return false;
        }

        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter;
        painter.begin(&image);
        painter.drawPixmap(0, 0, topLeft.width(), topLeft.height(), topLeft);
        painter.drawPixmap(topLeft.width(), 0, top.width(), top.height(), top);
        painter.drawPixmap(
            width - topRight.width(), 0, topRight.width(), topRight.height(), topRight);
        painter.drawPixmap(
            0, height - bottomLeft.height(), bottomLeft.width(), bottomLeft.height(), bottomLeft);
        painter.drawPixmap(
            bottomLeft.width(), height - bottom.height(), bottom.width(), bottom.height(), bottom);
        painter.drawPixmap(width - bottomRight.width(),
                           height - bottomRight.height(),
                           bottomRight.width(),
                           bottomRight.height(),
                           bottomRight);
        painter.drawPixmap(0, topLeft.height(), left.width(), left.height(), left);
        painter.drawPixmap(
            width - right.width(), topRight.height(), right.width(), right.height(), right);
        painter.end();

        m_texture = image;

        return true;
    }

private:
    QImage m_texture;
};

}
