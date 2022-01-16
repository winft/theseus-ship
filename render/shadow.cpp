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

#include "atoms.h"
#include "effects.h"
#include "render/compositor.h"
#include "toplevel.h"

#include "win/deco.h"
#include "win/scene.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationShadow>

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/shadow.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render
{

shadow::shadow(Toplevel* toplevel)
    : m_topLevel(toplevel)
    , m_cachedSize(toplevel->size())
    , m_decorationShadow(nullptr)
{
    QObject::connect(m_topLevel, &Toplevel::frame_geometry_changed, this, &shadow::geometryChanged);
}

shadow::~shadow()
{
}

shadow* shadow::createShadowFromX11(Toplevel* toplevel)
{
    auto data = shadow::readX11ShadowProperty(toplevel->xcb_window());
    if (!data.isEmpty()) {
        auto shadow = render::compositor::self()->scene()->createShadow(toplevel);

        if (!shadow->init(data)) {
            delete shadow;
            return nullptr;
        }
        return shadow;
    } else {
        return nullptr;
    }
}

shadow* shadow::createShadowFromDecoration(Toplevel* toplevel)
{
    if (!toplevel || !toplevel->control) {
        return nullptr;
    }
    if (!win::decoration(toplevel)) {
        return nullptr;
    }
    auto shadow = render::compositor::self()->scene()->createShadow(toplevel);
    if (!shadow->init(win::decoration(toplevel))) {
        delete shadow;
        return nullptr;
    }
    return shadow;
}

shadow* shadow::createShadowFromWayland(Toplevel* toplevel)
{
    auto surface = toplevel->surface();
    if (!surface) {
        return nullptr;
    }
    const auto s = surface->state().shadow;
    if (!s) {
        return nullptr;
    }
    auto shadow = render::compositor::self()->scene()->createShadow(toplevel);
    if (!shadow->init(s)) {
        delete shadow;
        return nullptr;
    }
    return shadow;
}

QVector<uint32_t> shadow::readX11ShadowProperty(xcb_window_t id)
{
    QVector<uint32_t> ret;
    if (id != XCB_WINDOW_NONE) {
        Xcb::Property property(false, id, atoms->kde_net_wm_shadow, XCB_ATOM_CARDINAL, 0, 12);
        uint32_t* shadow = property.value<uint32_t*>();
        if (shadow) {
            ret.reserve(12);
            for (int i = 0; i < 12; ++i) {
                ret << shadow[i];
            }
        }
    }
    return ret;
}

bool shadow::init(const QVector<uint32_t>& data)
{
    constexpr auto element_count = enum_index(shadow_element::count);

    QVector<Xcb::WindowGeometry> pixmapGeometries(element_count);
    QVector<xcb_get_image_cookie_t> getImageCookies(element_count);
    auto* c = connection();
    for (size_t i = 0; i < element_count; ++i) {
        pixmapGeometries[i] = Xcb::WindowGeometry(data[i]);
    }
    auto discardReplies = [&getImageCookies](int start) {
        for (int i = start; i < getImageCookies.size(); ++i) {
            xcb_discard_reply(connection(), getImageCookies.at(i).sequence);
        }
    };
    for (size_t i = 0; i < element_count; ++i) {
        auto& geo = pixmapGeometries[i];
        if (geo.isNull()) {
            discardReplies(0);
            return false;
        }
        getImageCookies[i] = xcb_get_image_unchecked(
            c, XCB_IMAGE_FORMAT_Z_PIXMAP, data[i], 0, 0, geo->width, geo->height, ~0);
    }
    for (size_t i = 0; i < element_count; ++i) {
        auto* reply = xcb_get_image_reply(c, getImageCookies.at(i), nullptr);
        if (!reply) {
            discardReplies(i + 1);
            return false;
        }
        auto& geo = pixmapGeometries[i];
        QImage image(xcb_get_image_data(reply), geo->width, geo->height, QImage::Format_ARGB32);
        m_shadowElements[i] = QPixmap::fromImage(image);
        free(reply);
    }
    m_topOffset = data[element_count];
    m_rightOffset = data[element_count + 1];
    m_bottomOffset = data[element_count + 2];
    m_leftOffset = data[element_count + 3];
    updateShadowRegion();
    if (!prepareBackend()) {
        return false;
    }
    buildQuads();
    return true;
}

bool shadow::init(KDecoration2::Decoration* decoration)
{
    if (m_decorationShadow) {
        // disconnect previous connections
        disconnect(m_decorationShadow.data(),
                   &KDecoration2::DecorationShadow::innerShadowRectChanged,
                   m_topLevel,
                   nullptr);
        disconnect(m_decorationShadow.data(),
                   &KDecoration2::DecorationShadow::shadowChanged,
                   m_topLevel,
                   nullptr);
        disconnect(m_decorationShadow.data(),
                   &KDecoration2::DecorationShadow::paddingChanged,
                   m_topLevel,
                   nullptr);
    }
    m_decorationShadow = decoration->shadow();
    if (!m_decorationShadow) {
        return false;
    }
    // setup connections - all just mapped to recreate
    auto update_shadow = [toplevel = m_topLevel]() { win::update_shadow(toplevel); };
    connect(m_decorationShadow.data(),
            &KDecoration2::DecorationShadow::innerShadowRectChanged,
            m_topLevel,
            update_shadow);
    connect(m_decorationShadow.data(),
            &KDecoration2::DecorationShadow::shadowChanged,
            m_topLevel,
            update_shadow);
    connect(m_decorationShadow.data(),
            &KDecoration2::DecorationShadow::paddingChanged,
            m_topLevel,
            update_shadow);

    const QMargins& p = m_decorationShadow->padding();
    m_topOffset = p.top();
    m_rightOffset = p.right();
    m_bottomOffset = p.bottom();
    m_leftOffset = p.left();
    updateShadowRegion();
    if (!prepareBackend()) {
        return false;
    }
    buildQuads();
    return true;
}

bool shadow::init(const QPointer<Wrapland::Server::Shadow>& shadow)
{
    if (!shadow) {
        return false;
    }

    m_shadowElements[enum_index(shadow_element::top)] = shadow->top()
        ? QPixmap::fromImage(shadow->top()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::top_right)] = shadow->topRight()
        ? QPixmap::fromImage(shadow->topRight()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::right)] = shadow->right()
        ? QPixmap::fromImage(shadow->right()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::bottom_right)] = shadow->bottomRight()
        ? QPixmap::fromImage(shadow->bottomRight()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::bottom)] = shadow->bottom()
        ? QPixmap::fromImage(shadow->bottom()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::bottom_left)] = shadow->bottomLeft()
        ? QPixmap::fromImage(shadow->bottomLeft()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::left)] = shadow->left()
        ? QPixmap::fromImage(shadow->left()->shmImage()->createQImage().copy())
        : QPixmap();
    m_shadowElements[enum_index(shadow_element::top_left)] = shadow->topLeft()
        ? QPixmap::fromImage(shadow->topLeft()->shmImage()->createQImage().copy())
        : QPixmap();

    const QMarginsF& p = shadow->offset();
    m_topOffset = p.top();
    m_rightOffset = p.right();
    m_bottomOffset = p.bottom();
    m_leftOffset = p.left();
    updateShadowRegion();
    if (!prepareBackend()) {
        return false;
    }
    buildQuads();
    return true;
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
    if (!m_topLevel) {
        return false;
    }

    if (m_decorationShadow) {
        if (m_topLevel->control) {
            if (auto deco = win::decoration(m_topLevel)) {
                if (init(deco)) {
                    return true;
                }
            }
        }
        return false;
    }

    if (kwinApp()->operationMode() != Application::OperationModeX11) {
        if (m_topLevel && m_topLevel->surface()) {
            if (const auto& s = m_topLevel->surface()->state().shadow) {
                if (init(s)) {
                    return true;
                }
            }
        }
    }

    auto data = shadow::readX11ShadowProperty(m_topLevel->xcb_window());
    if (data.isEmpty()) {
        return false;
    }

    init(data);

    return true;
}

void shadow::setToplevel(Toplevel* topLevel)
{
    // TODO(romangg): This function works because it is only used to change the toplevel to the
    //                remnant. But in general this would not clean up the connection from the ctor.
    m_topLevel = topLevel;
    connect(m_topLevel, &Toplevel::frame_geometry_changed, this, &shadow::geometryChanged);
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
