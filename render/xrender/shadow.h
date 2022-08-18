/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/shadow.h"

#include <kwinxrender/utils.h>

#include <QPainter>
#include <xcb/render.h>

namespace KWin::render::xrender
{

template<typename Window>
class shadow : public render::shadow<Window>
{
public:
    explicit shadow(Window* window)
        : render::shadow<Window>(window)
    {
        for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
            m_pictures[i] = nullptr;
        }
    }

    ~shadow() override
    {
        for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
            delete m_pictures[i];
        }
    }

    void layoutShadowRects(QRect& top,
                           QRect& topRight,
                           QRect& right,
                           QRect& bottomRight,
                           QRect& bottom,
                           QRect& bottomLeft,
                           QRect& left,
                           QRect& topLeft)
    {
        auto quads = this->shadowQuads();

        if (quads.count() == 0) {
            return;
        }

        WindowQuad topQuad = quads.select(WindowQuadShadowTop).at(0);
        WindowQuad topRightQuad = quads.select(WindowQuadShadowTopRight).at(0);
        WindowQuad topLeftQuad = quads.select(WindowQuadShadowTopLeft).at(0);
        WindowQuad leftQuad = quads.select(WindowQuadShadowLeft).at(0);
        WindowQuad rightQuad = quads.select(WindowQuadShadowRight).at(0);
        WindowQuad bottomQuad = quads.select(WindowQuadShadowBottom).at(0);
        WindowQuad bottomRightQuad = quads.select(WindowQuadShadowBottomRight).at(0);
        WindowQuad bottomLeftQuad = quads.select(WindowQuadShadowBottomLeft).at(0);

        top = QRect(topQuad.left(),
                    topQuad.top(),
                    (topQuad.right() - topQuad.left()),
                    (topQuad.bottom() - topQuad.top()));
        topLeft = QRect(topLeftQuad.left(),
                        topLeftQuad.top(),
                        (topLeftQuad.right() - topLeftQuad.left()),
                        (topLeftQuad.bottom() - topLeftQuad.top()));
        topRight = QRect(topRightQuad.left(),
                         topRightQuad.top(),
                         (topRightQuad.right() - topRightQuad.left()),
                         (topRightQuad.bottom() - topRightQuad.top()));
        left = QRect(leftQuad.left(),
                     leftQuad.top(),
                     (leftQuad.right() - leftQuad.left()),
                     (leftQuad.bottom() - leftQuad.top()));
        right = QRect(rightQuad.left(),
                      rightQuad.top(),
                      (rightQuad.right() - rightQuad.left()),
                      (rightQuad.bottom() - rightQuad.top()));
        bottom = QRect(bottomQuad.left(),
                       bottomQuad.top(),
                       (bottomQuad.right() - bottomQuad.left()),
                       (bottomQuad.bottom() - bottomQuad.top()));
        bottomLeft = QRect(bottomLeftQuad.left(),
                           bottomLeftQuad.top(),
                           (bottomLeftQuad.right() - bottomLeftQuad.left()),
                           (bottomLeftQuad.bottom() - bottomLeftQuad.top()));
        bottomRight = QRect(bottomRightQuad.left(),
                            bottomRightQuad.top(),
                            (bottomRightQuad.right() - bottomRightQuad.left()),
                            (bottomRightQuad.bottom() - bottomRightQuad.top()));
    }

    xcb_render_picture_t picture(shadow_element element) const
    {
        if (!m_pictures[enum_index(element)]) {
            return XCB_RENDER_PICTURE_NONE;
        }
        return *m_pictures[enum_index(element)];
    }

protected:
    void buildQuads() override
    {
        render::shadow<Window>::buildQuads();

        if (this->shadowQuads().count() == 0) {
            return;
        }

        QRect stlr, str, strr, srr, sbrr, sbr, sblr, slr;
        layoutShadowRects(str, strr, srr, sbrr, sbr, sblr, slr, stlr);
    }

    bool prepareBackend() override
    {
        if (this->hasDecorationShadow()) {
            auto const shadowImage = this->decorationShadowImage();
            QPainter p;
            int x = 0;
            int y = 0;
            auto drawElement = [this, &x, &y, &p, &shadowImage](auto element) {
                QPixmap pix(this->elementSize(element));
                pix.fill(Qt::transparent);
                p.begin(&pix);
                p.drawImage(0, 0, shadowImage, x, y, pix.width(), pix.height());
                p.end();
                this->setShadowElement(pix, element);
                return pix.size();
            };
            x += drawElement(shadow_element::top_left).width();
            x += drawElement(shadow_element::top).width();
            y += drawElement(shadow_element::top_right).height();
            drawElement(shadow_element::right);
            x = 0;
            y += drawElement(shadow_element::left).height();
            x += drawElement(shadow_element::bottom_left).width();
            x += drawElement(shadow_element::bottom).width();
            drawElement(shadow_element::bottom_right).width();
        }
        const uint32_t values[] = {XCB_RENDER_REPEAT_NORMAL};
        for (size_t i = 0; i < enum_index(shadow_element::count); ++i) {
            delete m_pictures[i];
            m_pictures[i]
                = new XRenderPicture(this->shadowPixmap(static_cast<shadow_element>(i)).toImage());
            xcb_render_change_picture(connection(), *m_pictures[i], XCB_RENDER_CP_REPEAT, values);
        }
        return true;
    }

private:
    XRenderPicture* m_pictures[static_cast<int>(shadow_element::count)];
};

}
