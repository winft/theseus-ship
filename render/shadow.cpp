/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Martin Gräßlin <mgraesslin@kde.org>

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
#include "shadow.h"

#include "compositor.h"
#include "deco_shadow.h"

#include "effects.h"
#include "toplevel.h"
#include "win/deco.h"
#include "win/scene.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationShadow>

namespace KWin::render
{

shadow::shadow(Toplevel* toplevel)
    : m_topLevel(toplevel)
    , m_cachedSize(toplevel->size())
{
    QObject::connect(m_topLevel, &Toplevel::frame_geometry_changed, this, &shadow::geometryChanged);
}

shadow::~shadow()
{
}

void shadow::updateShadowRegion()
{
    auto const size = m_topLevel->size();
    const QRect top(0, -m_topOffset, size.width(), m_topOffset);
    const QRect right(
        size.width(), -m_topOffset, m_rightOffset, size.height() + m_topOffset + m_bottomOffset);
    const QRect bottom(0, size.height(), size.width(), m_bottomOffset);
    const QRect left(
        -m_leftOffset, -m_topOffset, m_leftOffset, size.height() + m_topOffset + m_bottomOffset);
    m_shadowRegion = QRegion(top).united(right).united(bottom).united(left);
}

void shadow::buildQuads()
{
    // prepare window quads
    m_shadowQuads.clear();

    auto const size = m_topLevel->size();
    const QSize top(m_shadowElements[enum_index(shadow_element::top)].size());
    const QSize topRight(m_shadowElements[enum_index(shadow_element::top_right)].size());
    const QSize right(m_shadowElements[enum_index(shadow_element::right)].size());
    const QSize bottomRight(m_shadowElements[enum_index(shadow_element::bottom_right)].size());
    const QSize bottom(m_shadowElements[enum_index(shadow_element::bottom)].size());
    const QSize bottomLeft(m_shadowElements[enum_index(shadow_element::bottom_left)].size());
    const QSize left(m_shadowElements[enum_index(shadow_element::left)].size());
    const QSize topLeft(m_shadowElements[enum_index(shadow_element::top_left)].size());
    if ((left.width() - m_leftOffset > size.width())
        || (right.width() - m_rightOffset > size.width())
        || (top.height() - m_topOffset > size.height())
        || (bottom.height() - m_bottomOffset > size.height())) {
        // if our shadow is bigger than the window, we don't render the shadow
        m_shadowRegion = QRegion();
        return;
    }

    const QRect outerRect(QPoint(-m_leftOffset, -m_topOffset),
                          QPoint(size.width() + m_rightOffset, size.height() + m_bottomOffset));

    WindowQuad topLeftQuad(WindowQuadShadowTopLeft);
    topLeftQuad[0] = WindowVertex(outerRect.x(), outerRect.y(), 0.0, 0.0);
    topLeftQuad[1] = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y(), 1.0, 0.0);
    topLeftQuad[2]
        = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y() + topLeft.height(), 1.0, 1.0);
    topLeftQuad[3] = WindowVertex(outerRect.x(), outerRect.y() + topLeft.height(), 0.0, 1.0);
    m_shadowQuads.append(topLeftQuad);

    WindowQuad topQuad(WindowQuadShadowTop);
    topQuad[0] = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y(), 0.0, 0.0);
    topQuad[1] = WindowVertex(outerRect.right() - topRight.width(), outerRect.y(), 1.0, 0.0);
    topQuad[2] = WindowVertex(
        outerRect.right() - topRight.width(), outerRect.y() + top.height(), 1.0, 1.0);
    topQuad[3]
        = WindowVertex(outerRect.x() + topLeft.width(), outerRect.y() + top.height(), 0.0, 1.0);
    m_shadowQuads.append(topQuad);

    WindowQuad topRightQuad(WindowQuadShadowTopRight);
    topRightQuad[0] = WindowVertex(outerRect.right() - topRight.width(), outerRect.y(), 0.0, 0.0);
    topRightQuad[1] = WindowVertex(outerRect.right(), outerRect.y(), 1.0, 0.0);
    topRightQuad[2] = WindowVertex(outerRect.right(), outerRect.y() + topRight.height(), 1.0, 1.0);
    topRightQuad[3] = WindowVertex(
        outerRect.right() - topRight.width(), outerRect.y() + topRight.height(), 0.0, 1.0);
    m_shadowQuads.append(topRightQuad);

    WindowQuad rightQuad(WindowQuadShadowRight);
    rightQuad[0] = WindowVertex(
        outerRect.right() - right.width(), outerRect.y() + topRight.height(), 0.0, 0.0);
    rightQuad[1] = WindowVertex(outerRect.right(), outerRect.y() + topRight.height(), 1.0, 0.0);
    rightQuad[2]
        = WindowVertex(outerRect.right(), outerRect.bottom() - bottomRight.height(), 1.0, 1.0);
    rightQuad[3] = WindowVertex(
        outerRect.right() - right.width(), outerRect.bottom() - bottomRight.height(), 0.0, 1.0);
    m_shadowQuads.append(rightQuad);

    WindowQuad bottomRightQuad(WindowQuadShadowBottomRight);
    bottomRightQuad[0] = WindowVertex(outerRect.right() - bottomRight.width(),
                                      outerRect.bottom() - bottomRight.height(),
                                      0.0,
                                      0.0);
    bottomRightQuad[1]
        = WindowVertex(outerRect.right(), outerRect.bottom() - bottomRight.height(), 1.0, 0.0);
    bottomRightQuad[2] = WindowVertex(outerRect.right(), outerRect.bottom(), 1.0, 1.0);
    bottomRightQuad[3]
        = WindowVertex(outerRect.right() - bottomRight.width(), outerRect.bottom(), 0.0, 1.0);
    m_shadowQuads.append(bottomRightQuad);

    WindowQuad bottomQuad(WindowQuadShadowBottom);
    bottomQuad[0] = WindowVertex(
        outerRect.x() + bottomLeft.width(), outerRect.bottom() - bottom.height(), 0.0, 0.0);
    bottomQuad[1] = WindowVertex(
        outerRect.right() - bottomRight.width(), outerRect.bottom() - bottom.height(), 1.0, 0.0);
    bottomQuad[2]
        = WindowVertex(outerRect.right() - bottomRight.width(), outerRect.bottom(), 1.0, 1.0);
    bottomQuad[3] = WindowVertex(outerRect.x() + bottomLeft.width(), outerRect.bottom(), 0.0, 1.0);
    m_shadowQuads.append(bottomQuad);

    WindowQuad bottomLeftQuad(WindowQuadShadowBottomLeft);
    bottomLeftQuad[0]
        = WindowVertex(outerRect.x(), outerRect.bottom() - bottomLeft.height(), 0.0, 0.0);
    bottomLeftQuad[1] = WindowVertex(
        outerRect.x() + bottomLeft.width(), outerRect.bottom() - bottomLeft.height(), 1.0, 0.0);
    bottomLeftQuad[2]
        = WindowVertex(outerRect.x() + bottomLeft.width(), outerRect.bottom(), 1.0, 1.0);
    bottomLeftQuad[3] = WindowVertex(outerRect.x(), outerRect.bottom(), 0.0, 1.0);
    m_shadowQuads.append(bottomLeftQuad);

    WindowQuad leftQuad(WindowQuadShadowLeft);
    leftQuad[0] = WindowVertex(outerRect.x(), outerRect.y() + topLeft.height(), 0.0, 0.0);
    leftQuad[1]
        = WindowVertex(outerRect.x() + left.width(), outerRect.y() + topLeft.height(), 1.0, 0.0);
    leftQuad[2] = WindowVertex(
        outerRect.x() + left.width(), outerRect.bottom() - bottomLeft.height(), 1.0, 1.0);
    leftQuad[3] = WindowVertex(outerRect.x(), outerRect.bottom() - bottomLeft.height(), 0.0, 1.0);
    m_shadowQuads.append(leftQuad);
}

bool shadow::updateShadow()
{
    assert(m_topLevel);

    if (m_decorationShadow) {
        if (m_topLevel->control) {
            if (auto deco = win::decoration(m_topLevel)) {
                if (update_deco_shadow(*this, deco)) {
                    return true;
                }
            }
        }
        return false;
    }

    if (auto& win_update = m_topLevel->render->shadow_windowing.update; win_update) {
        return win_update(*this);
    }

    return false;
}

void shadow::geometryChanged()
{
    if (m_cachedSize == m_topLevel->size()) {
        return;
    }
    m_cachedSize = m_topLevel->size();
    updateShadowRegion();
    buildQuads();
}

QImage shadow::decorationShadowImage() const
{
    if (!m_decorationShadow) {
        return QImage();
    }
    return m_decorationShadow->shadow();
}

QSize shadow::elementSize(shadow_element element) const
{
    if (m_decorationShadow) {
        switch (element) {
        case shadow_element::top:
            return m_decorationShadow->topGeometry().size();
        case shadow_element::top_right:
            return m_decorationShadow->topRightGeometry().size();
        case shadow_element::right:
            return m_decorationShadow->rightGeometry().size();
        case shadow_element::bottom_right:
            return m_decorationShadow->bottomRightGeometry().size();
        case shadow_element::bottom:
            return m_decorationShadow->bottomGeometry().size();
        case shadow_element::bottom_left:
            return m_decorationShadow->bottomLeftGeometry().size();
        case shadow_element::left:
            return m_decorationShadow->leftGeometry().size();
        case shadow_element::top_left:
            return m_decorationShadow->topLeftGeometry().size();
        default:
            return QSize();
        }
    } else {
        return m_shadowElements[enum_index(element)].size();
    }
}

QMargins shadow::margins() const
{
    return QMargins(m_leftOffset, m_topOffset, m_rightOffset, m_topOffset);
}

void shadow::setShadowElement(const QPixmap& shadow, shadow_element element)
{
    m_shadowElements[enum_index(element)] = shadow;
}

} // namespace
