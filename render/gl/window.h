/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/window.h"

#include <kwingl/utils.h>

#include <QMatrix4x4>
#include <QVector4D>

namespace KWin::render::gl
{

class scene;
class texture;
class window_pixmap;

class window final : public render::window
{
public:
    enum Leaf {
        ShadowLeaf = 0,
        DecorationLeaf,
        ContentLeaf,
        PreviousContentLeaf,
        LeafCount,
    };

    struct LeafNode {
        LeafNode()
            : texture(nullptr)
            , firstVertex(0)
            , vertexCount(0)
            , opacity(1.0)
            , hasAlpha(false)
            , coordinateType(UnnormalizedCoordinates)
        {
        }

        GLTexture* texture;
        int firstVertex;
        int vertexCount;
        float opacity;
        bool hasAlpha;
        TextureCoordinateType coordinateType;
    };

    window(Toplevel* toplevel, gl::scene* scene);
    ~window() override;

    render::window_pixmap* createWindowPixmap() override;
    void performPaint(paint_type mask, QRegion region, WindowPaintData data) override;

private:
    QMatrix4x4 transformation(paint_type mask, const WindowPaintData& data) const;
    GLTexture* getDecorationTexture() const;
    QMatrix4x4 modelViewProjectionMatrix(paint_type mask, const WindowPaintData& data) const;
    QVector4D modulate(float opacity, float brightness) const;

    void setBlendEnabled(bool enabled);
    void setupLeafNodes(std::vector<LeafNode>& nodes,
                        std::vector<WindowQuadList> const& quads,
                        bool has_previous_content,
                        WindowPaintData const& data);

    bool beginRenderWindow(paint_type mask, const QRegion& region, WindowPaintData& data);
    void endRenderWindow();

    render::gl::texture* bindTexture();

    scene* m_scene;
    bool m_hardwareClipping{false};
    bool m_blendingEnabled{false};
};

class window_pixmap : public render::window_pixmap
{
public:
    window_pixmap(render::window* window, gl::scene* scene);
    ~window_pixmap() override;
    render::gl::texture* texture() const;
    bool bind();
    bool isValid() const override;

private:
    QScopedPointer<render::gl::texture> m_texture;
    scene* m_scene;
};

}
