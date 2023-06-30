/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"
#include "deco_renderer.h"
#include "shadow.h"

#include "render/window.h"
#include "win/deco/client_impl.h"

#include <kwineffects/paint_data.h>
#include <kwingl/utils.h>

#include <QMatrix4x4>
#include <QVector4D>
#include <cmath>

namespace KWin::render::gl
{

template<typename RefWin, typename Scene>
class window final : public Scene::window_t
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

    using type = window<RefWin, Scene>;
    using window_t = typename Scene::window_t;
    using buffer_t = typename Scene::buffer_t;

    window(RefWin ref_win, Scene& scene)
        : window_t(ref_win, *scene.platform.compositor)
        , scene{scene}
    {
        scene.windows.insert({this->id(), this});
    }

    ~window() override
    {
        scene.windows.erase(this->id());
    }

    render::buffer<window_t>* create_buffer() override
    {
        return new buffer_t(this, scene);
    }

    void performPaint(paint_type mask, effect::window_paint_data& data) override
    {
        if (!beginRenderWindow(mask, data)) {
            return;
        }

        QMatrix4x4 windowMatrix = transformation(mask, data);
        const QMatrix4x4 modelViewProjection = modelViewProjectionMatrix(mask, data);
        const QMatrix4x4 mvpMatrix = modelViewProjection * windowMatrix;

        auto shader = data.shader;
        if (!shader) {
            ShaderTraits traits = ShaderTrait::MapTexture;

            if (data.paint.opacity != 1.0 || data.paint.brightness != 1.0
                || data.cross_fade_progress != 1.0) {
                traits |= ShaderTrait::Modulate;
            }

            if (data.paint.saturation != 1.0) {
                traits |= ShaderTrait::AdjustSaturation;
            }

            shader = ShaderManager::instance()->pushShader(traits);
        }

        shader->setUniform(GLShader::ModelViewProjectionMatrix, mvpMatrix);
        shader->setUniform(GLShader::Saturation, data.paint.saturation);

        std::vector<WindowQuadList> quads;
        quads.resize(ContentLeaf + 1);
        int last_content_id = this->id();

        // TODO: remove again once we are sure that content ids never repeat.
        auto content_ids = std::vector<int>{last_content_id};

        // Split the quads into separate lists for each type
        for (auto const& quad : qAsConst(data.quads)) {
            switch (quad.type()) {
            case WindowQuadShadow:
                quads[ShadowLeaf].append(quad);
                continue;

            case WindowQuadDecoration:
                quads[DecorationLeaf].append(quad);
                continue;

            case WindowQuadContents:
                if (last_content_id != quad.id()) {
                    assert(!contains(content_ids, quad.id()));
                    // Content quads build chains in the list so an id never repeats itself.
                    quads.resize(quads.size() + 1);
                    last_content_id = quad.id();
                }
                quads.back().append(quad);
                continue;

            default:
                continue;
            }
        }

        bool has_previous_content = false;
        if (data.cross_fade_progress != 1.0) {
            auto previous = this->template previous_buffer<buffer_t>();
            if (previous) {
                has_previous_content = true;
                quads.resize(quads.size() + 1);
                auto const& old_content_rect = previous->win_integration->get_contents_rect();

                for (auto const& quad : qAsConst(quads[ContentLeaf])) {
                    if (quad.id() != static_cast<int>(this->id())) {
                        // We currently only do this for the main window and not annexed children
                        // that means we can skip from here on.
                        break;
                    }

                    // we need to create new window quads with normalize texture coordinates
                    // normal quads divide the x/y position by width/height. This would not work as
                    // the texture is larger than the visible content in case of a decorated Client
                    // resulting in garbage being shown. So we calculate the normalized texture
                    // coordinate in the Client's new content space and map it to the previous
                    // Client's content space.
                    WindowQuad newQuad(WindowQuadContents);
                    auto const content_geo
                        = std::visit(overload{[](auto&& ref_win) {
                                         return win::frame_relative_client_rect(ref_win);
                                     }},
                                     *this->ref_win);

                    for (int i = 0; i < 4; ++i) {
                        auto const xFactor = (quad[i].textureX() - content_geo.x())
                            / static_cast<double>(content_geo.width());
                        auto const yFactor = (quad[i].textureY() - content_geo.y())
                            / static_cast<double>(content_geo.height());

                        // TODO(romangg): How can these be interpreted?
                        auto const old_x
                            = xFactor * old_content_rect.width() + old_content_rect.x();
                        auto const old_y
                            = yFactor * old_content_rect.height() + old_content_rect.y();

                        // TODO(romangg): The get_size() call is only valid on X11!
                        WindowVertex vertex(quad[i].x(),
                                            quad[i].y(),
                                            old_x / previous->win_integration->get_size().width(),
                                            old_y / previous->win_integration->get_size().height());
                        newQuad[i] = vertex;
                    }

                    quads.back().append(newQuad);
                }
            }
        }

        const bool indexedQuads = GLVertexBuffer::supportsIndexedQuads();
        const GLenum primitiveType = indexedQuads ? GL_QUADS : GL_TRIANGLES;
        const int verticesPerQuad = indexedQuads ? 4 : 6;

        int quad_count = 0;
        for (auto const& quad_list : quads) {
            quad_count += quad_list.size();
        }

        auto const size = verticesPerQuad * quad_count * sizeof(GLVertex2D);

        GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
        GLVertex2D* map = static_cast<GLVertex2D*>(vbo->map(size));

        std::vector<LeafNode> nodes;
        setupLeafNodes(nodes, quads, has_previous_content, data);

        for (size_t i = 0, v = 0; i < quads.size(); i++) {
            if (quads[i].isEmpty() || !nodes[i].texture)
                continue;

            nodes[i].firstVertex = v;
            nodes[i].vertexCount = quads[i].count() * verticesPerQuad;

            const QMatrix4x4 matrix = nodes[i].texture->matrix(nodes[i].coordinateType);

            quads[i].makeInterleavedArrays(primitiveType, &map[v], matrix);
            v += quads[i].count() * verticesPerQuad;
        }

        vbo->unmap();
        vbo->bindArrays();

        // Make sure the blend function is set up correctly in case we will be doing blending
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        float opacity = -1.0;

        // The scissor region must be in the render target local coordinate system.
        QRegion scissorRegion = infiniteRegion();
        if (m_hardwareClipping) {
            scissorRegion = this->scene.mapToRenderTarget(data.paint.region);
        }

        for (size_t i = 0; i < quads.size(); i++) {
            if (nodes[i].vertexCount == 0)
                continue;

            setBlendEnabled(nodes[i].hasAlpha || nodes[i].opacity < 1.0);

            if (opacity != nodes[i].opacity) {
                shader->setUniform(GLShader::ModulationConstant,
                                   modulate(nodes[i].opacity, data.paint.brightness));
                opacity = nodes[i].opacity;
            }

            nodes[i].texture->setFilter(GL_LINEAR);
            nodes[i].texture->setWrapMode(GL_CLAMP_TO_EDGE);
            nodes[i].texture->bind();

            vbo->draw(scissorRegion,
                      primitiveType,
                      nodes[i].firstVertex,
                      nodes[i].vertexCount,
                      m_hardwareClipping);
        }

        vbo->unbindArrays();

        setBlendEnabled(false);

        if (!data.shader) {
            ShaderManager::instance()->popShader();
        }

        endRenderWindow();
    }

private:
    QMatrix4x4 transformation(paint_type mask, effect::window_paint_data const& data) const
    {
        QMatrix4x4 matrix;
        auto const win_pos = std::visit(
            overload{[&](auto&& ref_win) { return ref_win->geo.pos(); }}, *this->ref_win);
        matrix.translate(win_pos.x(), win_pos.y());

        if (!(mask & paint_type::window_transformed)) {
            return matrix;
        }

        matrix.translate(data.paint.geo.translation);
        matrix.scale(data.paint.geo.scale.x(), data.paint.geo.scale.y(), data.paint.geo.scale.z());

        if (data.paint.geo.rotation.angle == 0.0) {
            return matrix;
        }

        // Apply the rotation
        // cannot use data.rotation.applyTo(&matrix) as QGraphicsRotation uses projectedRotate to
        // map back to 2D
        matrix.translate(data.paint.geo.rotation.origin);
        auto const axis = data.paint.geo.rotation.axis;
        matrix.rotate(data.paint.geo.rotation.angle, axis.x(), axis.y(), axis.z());
        matrix.translate(-data.paint.geo.rotation.origin);

        return matrix;
    }

    GLTexture* getDecorationTexture() const
    {
        return std::visit(
            overload{[&](auto&& ref_win) -> GLTexture* {
                if (ref_win->control) {
                    if (ref_win->noBorder()) {
                        return nullptr;
                    }

                    if (!win::decoration(ref_win)) {
                        return nullptr;
                    }
                    using deco_renderer_t = deco_renderer<Scene>;
                    if (auto renderer = static_cast<deco_renderer_t*>(
                            ref_win->control->deco.client->renderer()->injector.get())) {
                        renderer->render();
                        return renderer->texture();
                    }
                } else if (auto& remnant = ref_win->remnant) {
                    if (!remnant->data.deco_render || remnant->data.no_border) {
                        return nullptr;
                    }
                    if (auto& renderer = remnant->data.deco_render) {
                        return static_cast<deco_render_data<Scene>&>(*renderer).texture.get();
                    }
                }
                return nullptr;
            }},
            *this->ref_win);
    }

    QMatrix4x4 modelViewProjectionMatrix(paint_type mask,
                                         effect::window_paint_data const& data) const
    {
        auto const pMatrix = data.paint.projection_matrix;
        auto const mvMatrix = data.paint.model_view_matrix;

        // An effect may want to override the default projection matrix in some cases,
        // such as when it is rendering a window on a render target that doesn't have
        // the same dimensions as the default framebuffer.
        //
        // Note that the screen transformation is not applied here.
        if (!pMatrix.isIdentity()) {
            return pMatrix * mvMatrix;
        }

        // If an effect has specified a model-view matrix, we multiply that matrix
        // with the default projection matrix.  If the effect hasn't specified a
        // model-view matrix, mvMatrix will be the identity matrix.
        if (flags(mask & paint_type::screen_transformed)) {
            return this->scene.screenProjectionMatrix() * mvMatrix;
        }

        return scene.projectionMatrix() * mvMatrix;
    }

    QVector4D modulate(float opacity, float brightness) const
    {
        const float a = opacity;
        const float rgb = opacity * brightness;

        return QVector4D(rgb, rgb, rgb, a);
    }

    void setBlendEnabled(bool enabled)
    {
        if (enabled && !m_blendingEnabled)
            glEnable(GL_BLEND);
        else if (!enabled && m_blendingEnabled)
            glDisable(GL_BLEND);

        m_blendingEnabled = enabled;
    }

    void setupLeafNodes(std::vector<LeafNode>& nodes,
                        std::vector<WindowQuadList> const& quads,
                        bool has_previous_content,
                        effect::window_paint_data const& data)
    {
        nodes.resize(quads.size());

        if (!quads[ShadowLeaf].isEmpty()) {
            nodes[ShadowLeaf].texture
                = static_cast<gl::shadow<window_t, Scene>*>(this->m_shadow.get())->shadowTexture();
            nodes[ShadowLeaf].opacity = data.paint.opacity;
            nodes[ShadowLeaf].hasAlpha = true;
            nodes[ShadowLeaf].coordinateType = NormalizedCoordinates;
        }

        if (!quads[DecorationLeaf].isEmpty()) {
            nodes[DecorationLeaf].texture = getDecorationTexture();
            nodes[DecorationLeaf].opacity = data.paint.opacity;
            nodes[DecorationLeaf].hasAlpha = true;
            nodes[DecorationLeaf].coordinateType = UnnormalizedCoordinates;
        }

        auto setup_content = [&data, &nodes](int index, auto window, auto texture) {
            auto& node = nodes[ContentLeaf + index];
            node.texture = texture;
            node.hasAlpha = !window->isOpaque();
            // TODO: ARGB crsoofading is atm. a hack, playing on opacities for two dumb SrcOver
            // operations Should be a shader
            if (data.cross_fade_progress != 1.0
                && (data.paint.opacity < 0.95
                    || std::visit(overload{[&](auto&& win) { return win::has_alpha(*win); }},
                                  *window->ref_win))) {
                float const opacity = 1.0 - data.cross_fade_progress;
                node.opacity
                    = data.paint.opacity * (1 - pow(opacity, 1.0f + 2.0f * data.paint.opacity));
            } else {
                node.opacity = data.paint.opacity;
            }
            node.coordinateType = UnnormalizedCoordinates;
        };

        setup_content(0, this, this->template get_buffer<buffer_t>()->texture.get());

        int contents_count = quads.size() - ContentLeaf;
        if (has_previous_content) {
            contents_count--;
        }

        for (int i = 1; i < contents_count; i++) {
            auto const& quad_list = quads[ContentLeaf + i];
            if (quad_list.isEmpty()) {
                continue;
            }

            auto& glscene = scene;
            auto it = glscene.windows.find(quad_list.front().id());
            if (it != glscene.windows.end()) {
                setup_content(i, it->second, it->second->bindTexture());
            }
        }

        if (has_previous_content) {
            auto previous = this->template previous_buffer<buffer_t>();
            auto const last = quads.size() - 1;
            nodes[last].texture = previous ? previous->texture.get() : nullptr;
            nodes[last].hasAlpha = !this->isOpaque();
            nodes[last].opacity = data.paint.opacity * (1.0 - data.cross_fade_progress);
            nodes[last].coordinateType = NormalizedCoordinates;
        }
    }

    bool beginRenderWindow(paint_type mask, effect::window_paint_data& data)
    {
        if (data.paint.region.isEmpty()) {
            return false;
        }

        m_hardwareClipping = data.paint.region != infiniteRegion()
            && flags(mask & paint_type::window_transformed)
            && !(mask & paint_type::screen_transformed);

        if (data.paint.region != infiniteRegion() && !m_hardwareClipping) {
            WindowQuadList quads;
            quads.reserve(data.quads.count());

            auto const win_pos
                = std::visit(overload{[&](auto&& win) { return win->geo.pos(); }}, *this->ref_win);
            auto const filterRegion = data.paint.region.translated(-win_pos.x(), -win_pos.y());

            // split all quads in bounding rect with the actual rects in the region
            for (auto const& quad : qAsConst(data.quads)) {
                for (auto const& r : filterRegion) {
                    QRectF const rf(r);
                    QRectF const quadRect(QPointF(quad.left(), quad.top()),
                                          QPointF(quad.right(), quad.bottom()));
                    QRectF const& intersected = rf.intersected(quadRect);
                    if (intersected.isValid()) {
                        if (quadRect == intersected) {
                            // case 1: completely contains, include and do not check other rects
                            quads << quad;
                            break;
                        }
                        // case 2: intersection
                        quads << quad.makeSubQuad(intersected.left(),
                                                  intersected.top(),
                                                  intersected.right(),
                                                  intersected.bottom());
                    }
                }
            }
            data.quads = quads;
        }

        if (data.quads.isEmpty()) {
            return false;
        }

        auto texture = bindTexture();
        if (!texture) {
            return false;
        }

        if (m_hardwareClipping) {
            glEnable(GL_SCISSOR_TEST);
        }

        // Update the texture filter
        if (scene.platform.base.operation_mode == base::operation_mode::x11) {
            if (flags(mask & (paint_type::window_transformed | paint_type::screen_transformed))) {
                this->filter = image_filter_type::good;
            } else {
                this->filter = image_filter_type::fast;
            }
            texture->setFilter(this->filter == image_filter_type::good ? GL_LINEAR : GL_NEAREST);
        } else {
            this->filter = image_filter_type::good;
            texture->setFilter(GL_LINEAR);
        }

        const GLVertexAttrib attribs[] = {
            {VA_Position, 2, GL_FLOAT, offsetof(GLVertex2D, position)},
            {VA_TexCoord, 2, GL_FLOAT, offsetof(GLVertex2D, texcoord)},
        };

        auto vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        vbo->setAttribLayout(attribs, 2, sizeof(GLVertex2D));

        return true;
    }

    void endRenderWindow()
    {
        if (m_hardwareClipping) {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    typename Scene::texture_t* bindTexture()
    {
        auto buffer = this->template get_buffer<buffer_t>();
        if (!buffer) {
            return nullptr;
        }
        if (buffer->isDiscarded()) {
            return buffer->texture.get();
        }

        if (!std::visit(
                overload{[&](auto&& win) { return win->render_data.damage_region.isEmpty(); }},
                *this->ref_win))
            scene.insertWait();

        if (!buffer->bind()) {
            return nullptr;
        }
        return buffer->texture.get();
    }

    bool m_hardwareClipping{false};
    bool m_blendingEnabled{false};
    Scene& scene;
};

}
