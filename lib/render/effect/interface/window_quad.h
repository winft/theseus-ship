/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>
#include <render/effect/interface/types.h>

#include <QRegion>
#include <QVector2D>
#include <QVector3D>
#include <QVector>

namespace KWin
{

struct GLVertex2D {
    QVector2D position;
    QVector2D texcoord;
};

struct GLVertex3D {
    QVector3D position;
    QVector2D texcoord;
};

/**
 * @short Vertex class
 *
 * A vertex is one position in a window. WindowQuad consists of four WindowVertex objects
 * and represents one part of a window.
 */
class WindowVertex
{
public:
    WindowVertex()
        : px(0)
        , py(0)
        , ox(0)
        , oy(0)
        , tx(0)
        , ty(0)
    {
    }

    WindowVertex(const QPointF& position, const QPointF& textureCoordinate)
        : px(position.x())
        , py(position.y())
        , ox(position.x())
        , oy(position.y())
        , tx(textureCoordinate.x())
        , ty(textureCoordinate.y())
    {
    }

    WindowVertex(double x, double y, double tx, double ty)
        : px(x)
        , py(y)
        , ox(x)
        , oy(y)
        , tx(tx)
        , ty(ty)
    {
    }

    double x() const
    {
        return px;
    }
    double y() const
    {
        return py;
    }
    double u() const
    {
        return tx;
    }
    double v() const
    {
        return ty;
    }
    double originalX() const
    {
        return ox;
    }
    double originalY() const
    {
        return oy;
    }
    double textureX() const
    {
        return tx;
    }
    double textureY() const
    {
        return ty;
    }
    void move(double x, double y)
    {
        px = x;
        py = y;
    }
    void setX(double x)
    {
        px = x;
    }
    void setY(double y)
    {
        py = y;
    }

private:
    friend class WindowQuad;
    friend class WindowQuadList;
    double px, py; // position
    double ox, oy; // origional position
    double tx, ty; // texture coords
};

/**
 * @short Class representing one area of a window.
 *
 * WindowQuads consists of four WindowVertex objects and represents one part of a window.
 */
// NOTE: This class expects the (original) vertices to be in the clockwise order starting from
// topleft.
class KWIN_EXPORT WindowQuad
{
public:
    explicit WindowQuad(WindowQuadType type, int id = -1);
    WindowQuad makeSubQuad(double x1, double y1, double x2, double y2) const;
    WindowVertex& operator[](int index);
    const WindowVertex& operator[](int index) const;
    WindowQuadType type() const;
    void setUVAxisSwapped(bool value)
    {
        uvSwapped = value;
    }
    bool uvAxisSwapped() const
    {
        return uvSwapped;
    }
    int id() const;
    bool decoration() const;
    bool effect() const;
    double left() const;
    double right() const;
    double top() const;
    double bottom() const;
    double originalLeft() const;
    double originalRight() const;
    double originalTop() const;
    double originalBottom() const;
    bool smoothNeeded() const;
    bool isTransformed() const;

private:
    friend class WindowQuadList;
    WindowVertex verts[4];
    WindowQuadType quadType; // 0 - contents, 1 - decoration
    bool uvSwapped;
    int quadID;
};

class KWIN_EXPORT WindowQuadList : public QVector<WindowQuad>
{
public:
    WindowQuadList splitAtX(double x) const;
    WindowQuadList splitAtY(double y) const;
    WindowQuadList makeGrid(int maxquadsize) const;
    WindowQuadList makeRegularGrid(int xSubdivisions, int ySubdivisions) const;
    WindowQuadList select(WindowQuadType type) const;
    WindowQuadList filterOut(WindowQuadType type) const;
    bool smoothNeeded() const;
    void
    makeInterleavedArrays(unsigned int type, GLVertex2D* vertices, const QMatrix4x4& matrix) const;
    void makeArrays(float** vertices, float** texcoords, const QSizeF& size, bool yInverted) const;
    bool isTransformed() const;
};

}
