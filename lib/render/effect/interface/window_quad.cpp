/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_quad.h"

#include <QMatrix4x4>
#include <QtMath>

namespace KWin
{

WindowQuad::WindowQuad(WindowQuadType t, int id)
    : quadType(t)
    , uvSwapped(false)
    , quadID(id)
{
}

WindowVertex& WindowQuad::operator[](int index)
{
    Q_ASSERT(index >= 0 && index < 4);
    return verts[index];
}

const WindowVertex& WindowQuad::operator[](int index) const
{
    Q_ASSERT(index >= 0 && index < 4);
    return verts[index];
}

WindowQuadType WindowQuad::type() const
{
    Q_ASSERT(quadType != WindowQuadError);
    return quadType;
}

int WindowQuad::id() const
{
    return quadID;
}

bool WindowQuad::decoration() const
{
    Q_ASSERT(quadType != WindowQuadError);
    return quadType == WindowQuadDecoration;
}

bool WindowQuad::effect() const
{
    Q_ASSERT(quadType != WindowQuadError);
    return quadType >= EFFECT_QUAD_TYPE_START;
}

bool WindowQuad::isTransformed() const
{
    return !(verts[0].px == verts[0].ox && verts[0].py == verts[0].oy && verts[1].px == verts[1].ox
             && verts[1].py == verts[1].oy && verts[2].px == verts[2].ox
             && verts[2].py == verts[2].oy && verts[3].px == verts[3].ox
             && verts[3].py == verts[3].oy);
}

double WindowQuad::left() const
{
    return qMin(verts[0].px, qMin(verts[1].px, qMin(verts[2].px, verts[3].px)));
}

double WindowQuad::right() const
{
    return qMax(verts[0].px, qMax(verts[1].px, qMax(verts[2].px, verts[3].px)));
}

double WindowQuad::top() const
{
    return qMin(verts[0].py, qMin(verts[1].py, qMin(verts[2].py, verts[3].py)));
}

double WindowQuad::bottom() const
{
    return qMax(verts[0].py, qMax(verts[1].py, qMax(verts[2].py, verts[3].py)));
}

double WindowQuad::originalLeft() const
{
    return verts[0].ox;
}

double WindowQuad::originalRight() const
{
    return verts[2].ox;
}

double WindowQuad::originalTop() const
{
    return verts[0].oy;
}

double WindowQuad::originalBottom() const
{
    return verts[2].oy;
}

WindowQuad WindowQuad::makeSubQuad(double x1, double y1, double x2, double y2) const
{
    Q_ASSERT(x1 < x2 && y1 < y2 && x1 >= left() && x2 <= right() && y1 >= top() && y2 <= bottom());
#if !defined(QT_NO_DEBUG)
    if (isTransformed())
        qFatal("Splitting quads is allowed only in pre-paint calls!");
#endif
    WindowQuad ret(*this);
    // vertices are clockwise starting from topleft
    ret.verts[0].px = x1;
    ret.verts[3].px = x1;
    ret.verts[1].px = x2;
    ret.verts[2].px = x2;
    ret.verts[0].py = y1;
    ret.verts[1].py = y1;
    ret.verts[2].py = y2;
    ret.verts[3].py = y2;
    // original x/y are supposed to be the same, no transforming is done here
    ret.verts[0].ox = x1;
    ret.verts[3].ox = x1;
    ret.verts[1].ox = x2;
    ret.verts[2].ox = x2;
    ret.verts[0].oy = y1;
    ret.verts[1].oy = y1;
    ret.verts[2].oy = y2;
    ret.verts[3].oy = y2;

    const double my_u0 = verts[0].tx;
    const double my_u1 = verts[2].tx;
    const double my_v0 = verts[0].ty;
    const double my_v1 = verts[2].ty;

    const double width = right() - left();
    const double height = bottom() - top();

    const double texWidth = my_u1 - my_u0;
    const double texHeight = my_v1 - my_v0;

    if (!uvAxisSwapped()) {
        const double u0 = (x1 - left()) / width * texWidth + my_u0;
        const double u1 = (x2 - left()) / width * texWidth + my_u0;
        const double v0 = (y1 - top()) / height * texHeight + my_v0;
        const double v1 = (y2 - top()) / height * texHeight + my_v0;

        ret.verts[0].tx = u0;
        ret.verts[3].tx = u0;
        ret.verts[1].tx = u1;
        ret.verts[2].tx = u1;
        ret.verts[0].ty = v0;
        ret.verts[1].ty = v0;
        ret.verts[2].ty = v1;
        ret.verts[3].ty = v1;
    } else {
        const double u0 = (y1 - top()) / height * texWidth + my_u0;
        const double u1 = (y2 - top()) / height * texWidth + my_u0;
        const double v0 = (x1 - left()) / width * texHeight + my_v0;
        const double v1 = (x2 - left()) / width * texHeight + my_v0;

        ret.verts[0].tx = u0;
        ret.verts[1].tx = u0;
        ret.verts[2].tx = u1;
        ret.verts[3].tx = u1;
        ret.verts[0].ty = v0;
        ret.verts[3].ty = v0;
        ret.verts[1].ty = v1;
        ret.verts[2].ty = v1;
    }

    ret.setUVAxisSwapped(uvAxisSwapped());

    return ret;
}

bool WindowQuad::smoothNeeded() const
{
    // smoothing is needed if the width or height of the quad does not match the original size
    double width = verts[1].ox - verts[0].ox;
    double height = verts[2].oy - verts[1].oy;
    return (verts[1].px - verts[0].px != width || verts[2].px - verts[3].px != width
            || verts[2].py - verts[1].py != height || verts[3].py - verts[0].py != height);
}

WindowQuadList WindowQuadList::splitAtX(double x) const
{
    WindowQuadList ret;
    ret.reserve(count());
    for (const WindowQuad& quad : *this) {
#if !defined(QT_NO_DEBUG)
        if (quad.isTransformed())
            qFatal("Splitting quads is allowed only in pre-paint calls!");
#endif
        bool wholeleft = true;
        bool wholeright = true;
        for (int i = 0; i < 4; ++i) {
            if (quad[i].x() < x)
                wholeright = false;
            if (quad[i].x() > x)
                wholeleft = false;
        }
        if (wholeleft || wholeright) { // is whole in one split part
            ret.append(quad);
            continue;
        }
        if (quad.top() == quad.bottom() || quad.left() == quad.right()) { // quad has no size
            ret.append(quad);
            continue;
        }
        ret.append(quad.makeSubQuad(quad.left(), quad.top(), x, quad.bottom()));
        ret.append(quad.makeSubQuad(x, quad.top(), quad.right(), quad.bottom()));
    }
    return ret;
}

WindowQuadList WindowQuadList::splitAtY(double y) const
{
    WindowQuadList ret;
    ret.reserve(count());
    for (const WindowQuad& quad : *this) {
#if !defined(QT_NO_DEBUG)
        if (quad.isTransformed())
            qFatal("Splitting quads is allowed only in pre-paint calls!");
#endif
        bool wholetop = true;
        bool wholebottom = true;
        for (int i = 0; i < 4; ++i) {
            if (quad[i].y() < y)
                wholebottom = false;
            if (quad[i].y() > y)
                wholetop = false;
        }
        if (wholetop || wholebottom) { // is whole in one split part
            ret.append(quad);
            continue;
        }
        if (quad.top() == quad.bottom() || quad.left() == quad.right()) { // quad has no size
            ret.append(quad);
            continue;
        }
        ret.append(quad.makeSubQuad(quad.left(), quad.top(), quad.right(), y));
        ret.append(quad.makeSubQuad(quad.left(), y, quad.right(), quad.bottom()));
    }
    return ret;
}

WindowQuadList WindowQuadList::makeGrid(int maxQuadSize) const
{
    if (empty())
        return *this;

    // Find the bounding rectangle
    double left = first().left();
    double right = first().right();
    double top = first().top();
    double bottom = first().bottom();

    for (auto const& quad : qAsConst(*this)) {
#if !defined(QT_NO_DEBUG)
        if (quad.isTransformed())
            qFatal("Splitting quads is allowed only in pre-paint calls!");
#endif
        left = qMin(left, quad.left());
        right = qMax(right, quad.right());
        top = qMin(top, quad.top());
        bottom = qMax(bottom, quad.bottom());
    }

    WindowQuadList ret;

    for (const WindowQuad& quad : *this) {
        const double quadLeft = quad.left();
        const double quadRight = quad.right();
        const double quadTop = quad.top();
        const double quadBottom = quad.bottom();

        // sanity check, see BUG 390953
        if (quadLeft == quadRight || quadTop == quadBottom) {
            ret.append(quad);
            continue;
        }

        // Compute the top-left corner of the first intersecting grid cell
        const double xBegin = left + qFloor((quadLeft - left) / maxQuadSize) * maxQuadSize;
        const double yBegin = top + qFloor((quadTop - top) / maxQuadSize) * maxQuadSize;

        // Loop over all intersecting cells and add sub-quads
        for (double y = yBegin; y < quadBottom; y += maxQuadSize) {
            const double y0 = qMax(y, quadTop);
            const double y1 = qMin(quadBottom, y + maxQuadSize);

            for (double x = xBegin; x < quadRight; x += maxQuadSize) {
                const double x0 = qMax(x, quadLeft);
                const double x1 = qMin(quadRight, x + maxQuadSize);

                ret.append(quad.makeSubQuad(x0, y0, x1, y1));
            }
        }
    }

    return ret;
}

WindowQuadList WindowQuadList::makeRegularGrid(int xSubdivisions, int ySubdivisions) const
{
    if (empty())
        return *this;

    // Find the bounding rectangle
    double left = first().left();
    double right = first().right();
    double top = first().top();
    double bottom = first().bottom();

    for (const WindowQuad& quad : *this) {
#if !defined(QT_NO_DEBUG)
        if (quad.isTransformed())
            qFatal("Splitting quads is allowed only in pre-paint calls!");
#endif
        left = qMin(left, quad.left());
        right = qMax(right, quad.right());
        top = qMin(top, quad.top());
        bottom = qMax(bottom, quad.bottom());
    }

    double xIncrement = (right - left) / xSubdivisions;
    double yIncrement = (bottom - top) / ySubdivisions;

    WindowQuadList ret;

    for (const WindowQuad& quad : *this) {
        const double quadLeft = quad.left();
        const double quadRight = quad.right();
        const double quadTop = quad.top();
        const double quadBottom = quad.bottom();

        // sanity check, see BUG 390953
        if (quadLeft == quadRight || quadTop == quadBottom) {
            ret.append(quad);
            continue;
        }

        // Compute the top-left corner of the first intersecting grid cell
        const double xBegin = left + qFloor((quadLeft - left) / xIncrement) * xIncrement;
        const double yBegin = top + qFloor((quadTop - top) / yIncrement) * yIncrement;

        // Loop over all intersecting cells and add sub-quads
        for (double y = yBegin; y < quadBottom; y += yIncrement) {
            const double y0 = qMax(y, quadTop);
            const double y1 = qMin(quadBottom, y + yIncrement);

            for (double x = xBegin; x < quadRight; x += xIncrement) {
                const double x0 = qMax(x, quadLeft);
                const double x1 = qMin(quadRight, x + xIncrement);

                ret.append(quad.makeSubQuad(x0, y0, x1, y1));
            }
        }
    }

    return ret;
}

#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif

#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif

void WindowQuadList::makeInterleavedArrays(unsigned int type,
                                           std::span<GLVertex2D> vertices,
                                           QMatrix4x4 const& textureMatrix) const
{
    // Since we know that the texture matrix just scales and translates
    // we can use this information to optimize the transformation
    const QVector2D coeff(textureMatrix(0, 0), textureMatrix(1, 1));
    const QVector2D offset(textureMatrix(0, 3), textureMatrix(1, 3));

    size_t index = 0;
    Q_ASSERT(type == GL_QUADS || type == GL_TRIANGLES);

    switch (type) {
    case GL_QUADS: {
        for (const WindowQuad& quad : *this) {
#pragma GCC unroll 4
            for (int j = 0; j < 4; j++) {
                const WindowVertex& wv = quad[j];

                GLVertex2D v;
                v.position = QVector2D(wv.x(), wv.y());
                v.texcoord = QVector2D(wv.u(), wv.v()) * coeff + offset;

                vertices[index++] = v;
            }
        }
        break;
    }
    case GL_TRIANGLES: {
        for (const WindowQuad& quad : *this) {
            GLVertex2D v[4]; // Four unique vertices / quad
#pragma GCC unroll 4
            for (int j = 0; j < 4; j++) {
                const WindowVertex& wv = quad[j];

                v[j].position = QVector2D(wv.x(), wv.y());
                v[j].texcoord = QVector2D(wv.u(), wv.v()) * coeff + offset;
            }

            // First triangle
            vertices[index++] = v[1]; // Top-right
            vertices[index++] = v[0]; // Top-left
            vertices[index++] = v[3]; // Bottom-left

            // Second triangle
            vertices[index++] = v[3]; // Bottom-left
            vertices[index++] = v[2]; // Bottom-right
            vertices[index++] = v[1]; // Top-right
        }
        break;
    }
    default:
        break;
    }
}

void WindowQuadList::makeArrays(float** vertices,
                                float** texcoords,
                                const QSizeF& size,
                                bool yInverted) const
{
    *vertices = new float[count() * 6 * 2];
    *texcoords = new float[count() * 6 * 2];

    float* vpos = *vertices;
    float* tpos = *texcoords;

    // Note: The positions in a WindowQuad are stored in clockwise order
    const int index[] = {1, 0, 3, 3, 2, 1};

    for (const WindowQuad& quad : *this) {
        for (int j = 0; j < 6; j++) {
            const WindowVertex& wv = quad[index[j]];

            *vpos++ = wv.x();
            *vpos++ = wv.y();

            *tpos++ = wv.u() / size.width();
            *tpos++ = yInverted ? (wv.v() / size.height()) : (1.0 - wv.v() / size.height());
        }
    }
}

WindowQuadList WindowQuadList::select(WindowQuadType type) const
{
    for (auto const& q : qAsConst(*this)) {
        if (q.type() != type) { // something else than ones to select, make a copy and filter
            WindowQuadList ret;
            for (auto const& q : qAsConst(*this)) {
                if (q.type() == type)
                    ret.append(q);
            }
            return ret;
        }
    }
    return *this; // nothing to filter out
}

WindowQuadList WindowQuadList::filterOut(WindowQuadType type) const
{
    for (const WindowQuad& q : *this) {
        if (q.type() == type) { // something to filter out, make a copy and filter
            WindowQuadList ret;
            for (auto const& q : qAsConst(*this)) {
                if (q.type() != type)
                    ret.append(q);
            }
            return ret;
        }
    }
    return *this; // nothing to filter out
}

bool WindowQuadList::smoothNeeded() const
{
    return std::any_of(
        constBegin(), constEnd(), [](const WindowQuad& q) { return q.smoothNeeded(); });
}

bool WindowQuadList::isTransformed() const
{
    return std::any_of(
        constBegin(), constEnd(), [](const WindowQuad& q) { return q.isTransformed(); });
}

}
