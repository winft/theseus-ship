/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009, 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "base/logging.h"
#include "deco_renderer.h"
#include "scene.h"
#include "shadow.h"
#include "texture.h"

#include "win/geo.h"

#include <Wrapland/Server/surface.h>
#include <cmath>

namespace KWin::render::gl
{

window::window(Toplevel* toplevel, gl::scene* scene)
    : render::window(toplevel)
    , m_scene(scene)
{
    m_scene->windows.insert({id(), this});
}

window::~window()
{
    m_scene->windows.erase(id());
}

// Bind the window pixmap to an OpenGL texture.
render::gl::texture* window::bindTexture()
{
    auto pixmap = windowPixmap<window_pixmap>();
    if (!pixmap) {
        return nullptr;
    }
    if (pixmap->isDiscarded()) {
        return pixmap->texture();
    }

    if (!get_window()->damage().isEmpty())
        m_scene->insertWait();

    if (!pixmap->bind()) {
        return nullptr;
    }
    return pixmap->texture();
    ;
}

QMatrix4x4 window::transformation(paint_type mask, const WindowPaintData& data) const
{
    QMatrix4x4 matrix;
    matrix.translate(x(), y());

    if (!(mask & paint_type::window_transformed)) {
        return matrix;
    }

    matrix.translate(data.translation());
    const QVector3D scale = data.scale();
    matrix.scale(scale.x(), scale.y(), scale.z());

    if (data.rotationAngle() == 0.0)
        return matrix;

    // Apply the rotation
    // cannot use data.rotation.applyTo(&matrix) as QGraphicsRotation uses projectedRotate to map
    // back to 2D
    matrix.translate(data.rotationOrigin());
    const QVector3D axis = data.rotationAxis();
    matrix.rotate(data.rotationAngle(), axis.x(), axis.y(), axis.z());
    matrix.translate(-data.rotationOrigin());

    return matrix;
}

bool window::beginRenderWindow(paint_type mask, const QRegion& region, WindowPaintData& data)
{
    if (region.isEmpty())
        return false;

    m_hardwareClipping = region != infiniteRegion() && flags(mask & paint_type::window_transformed)
        && !(mask & paint_type::screen_transformed);
    if (region != infiniteRegion() && !m_hardwareClipping) {
        WindowQuadList quads;
        quads.reserve(data.quads.count());

        const QRegion filterRegion = region.translated(-x(), -y());
        // split all quads in bounding rect with the actual rects in the region
        for (const WindowQuad& quad : qAsConst(data.quads)) {
            for (const QRect& r : filterRegion) {
                const QRectF rf(r);
                const QRectF quadRect(QPointF(quad.left(), quad.top()),
                                      QPointF(quad.right(), quad.bottom()));
                const QRectF& intersected = rf.intersected(quadRect);
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

    if (data.quads.isEmpty())
        return false;

    auto texture = bindTexture();
    if (!texture) {
        return false;
    }

    if (m_hardwareClipping) {
        glEnable(GL_SCISSOR_TEST);
    }

    // Update the texture filter
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        if (flags(mask & (paint_type::window_transformed | paint_type::screen_transformed))) {
            filter = image_filter_type::good;
        } else {
            filter = image_filter_type::fast;
        }
        texture->setFilter(filter == image_filter_type::good ? GL_LINEAR : GL_NEAREST);
    } else {
        filter = image_filter_type::good;
        texture->setFilter(GL_LINEAR);
    }

    const GLVertexAttrib attribs[] = {
        {VA_Position, 2, GL_FLOAT, offsetof(GLVertex2D, position)},
        {VA_TexCoord, 2, GL_FLOAT, offsetof(GLVertex2D, texcoord)},
    };

    GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(attribs, 2, sizeof(GLVertex2D));

    return true;
}

void window::endRenderWindow()
{
    if (m_hardwareClipping) {
        glDisable(GL_SCISSOR_TEST);
    }
}

GLTexture* window::getDecorationTexture() const
{
    if (toplevel->control) {
        if (toplevel->noBorder()) {
            return nullptr;
        }

        if (!win::decoration(toplevel)) {
            return nullptr;
        }
        if (auto renderer
            = static_cast<deco_renderer*>(toplevel->control->deco().client->renderer())) {
            renderer->render();
            return renderer->texture();
        }
    } else if (auto remnant = toplevel->remnant()) {
        if (!remnant->control || remnant->no_border) {
            return nullptr;
        }
        if (auto renderer = static_cast<const deco_renderer*>(remnant->decoration_renderer)) {
            return renderer->texture();
        }
    }
    return nullptr;
}

render::window_pixmap* window::createWindowPixmap()
{
    return new window_pixmap(this, m_scene);
}

QVector4D window::modulate(float opacity, float brightness) const
{
    const float a = opacity;
    const float rgb = opacity * brightness;

    return QVector4D(rgb, rgb, rgb, a);
}

void window::setBlendEnabled(bool enabled)
{
    if (enabled && !m_blendingEnabled)
        glEnable(GL_BLEND);
    else if (!enabled && m_blendingEnabled)
        glDisable(GL_BLEND);

    m_blendingEnabled = enabled;
}

void window::setupLeafNodes(std::vector<LeafNode>& nodes,
                            std::vector<WindowQuadList> const& quads,
                            bool has_previous_content,
                            WindowPaintData const& data)
{
    nodes.resize(quads.size());

    if (!quads[ShadowLeaf].isEmpty()) {
        nodes[ShadowLeaf].texture = static_cast<gl::shadow*>(m_shadow)->shadowTexture();
        nodes[ShadowLeaf].opacity = data.opacity();
        nodes[ShadowLeaf].hasAlpha = true;
        nodes[ShadowLeaf].coordinateType = NormalizedCoordinates;
    }

    if (!quads[DecorationLeaf].isEmpty()) {
        nodes[DecorationLeaf].texture = getDecorationTexture();
        nodes[DecorationLeaf].opacity = data.opacity();
        nodes[DecorationLeaf].hasAlpha = true;
        nodes[DecorationLeaf].coordinateType = UnnormalizedCoordinates;
    }

    auto setup_content = [&data, &nodes](int index, window* window, render::gl::texture* texture) {
        auto& node = nodes[ContentLeaf + index];
        node.texture = texture;
        node.hasAlpha = !window->isOpaque();
        // TODO: ARGB crsoofading is atm. a hack, playing on opacities for two dumb SrcOver
        // operations Should be a shader
        if (data.crossFadeProgress() != 1.0
            && (data.opacity() < 0.95 || window->toplevel->hasAlpha())) {
            const float opacity = 1.0 - data.crossFadeProgress();
            node.opacity = data.opacity() * (1 - pow(opacity, 1.0f + 2.0f * data.opacity()));
        } else {
            node.opacity = data.opacity();
        }
        node.coordinateType = UnnormalizedCoordinates;
    };

    setup_content(0, this, windowPixmap<window_pixmap>()->texture());

    int contents_count = quads.size() - ContentLeaf;
    if (has_previous_content) {
        contents_count--;
    }

    for (int i = 1; i < contents_count; i++) {
        auto const& quad_list = quads[ContentLeaf + i];
        if (quad_list.isEmpty()) {
            continue;
        }

        auto it = m_scene->windows.find(quad_list.front().id());
        if (it != m_scene->windows.end()) {
            setup_content(i, it->second, it->second->bindTexture());
        }
    }

    if (has_previous_content) {
        auto previous = previousWindowPixmap<window_pixmap>();
        auto const last = quads.size() - 1;
        nodes[last].texture = previous ? previous->texture() : nullptr;
        nodes[last].hasAlpha = !isOpaque();
        nodes[last].opacity = data.opacity() * (1.0 - data.crossFadeProgress());
        nodes[last].coordinateType = NormalizedCoordinates;
    }
}

QMatrix4x4 window::modelViewProjectionMatrix(paint_type mask, const WindowPaintData& data) const
{
    const QMatrix4x4 pMatrix = data.projectionMatrix();
    const QMatrix4x4 mvMatrix = data.modelViewMatrix();

    // An effect may want to override the default projection matrix in some cases,
    // such as when it is rendering a window on a render target that doesn't have
    // the same dimensions as the default framebuffer.
    //
    // Note that the screen transformation is not applied here.
    if (!pMatrix.isIdentity())
        return pMatrix * mvMatrix;

    // If an effect has specified a model-view matrix, we multiply that matrix
    // with the default projection matrix.  If the effect hasn't specified a
    // model-view matrix, mvMatrix will be the identity matrix.
    if (flags(mask & paint_type::screen_transformed)) {
        return m_scene->screenProjectionMatrix() * mvMatrix;
    }

    return m_scene->projectionMatrix() * mvMatrix;
}

void window::performPaint(paint_type mask, QRegion region, WindowPaintData data)
{
    if (!beginRenderWindow(mask, region, data))
        return;

    QMatrix4x4 windowMatrix = transformation(mask, data);
    const QMatrix4x4 modelViewProjection = modelViewProjectionMatrix(mask, data);
    const QMatrix4x4 mvpMatrix = modelViewProjection * windowMatrix;

    GLShader* shader = data.shader;
    if (!shader) {
        ShaderTraits traits = ShaderTrait::MapTexture;

        if (data.opacity() != 1.0 || data.brightness() != 1.0 || data.crossFadeProgress() != 1.0)
            traits |= ShaderTrait::Modulate;

        if (data.saturation() != 1.0)
            traits |= ShaderTrait::AdjustSaturation;

        shader = ShaderManager::instance()->pushShader(traits);
    }
    shader->setUniform(GLShader::ModelViewProjectionMatrix, mvpMatrix);

    shader->setUniform(GLShader::Saturation, data.saturation());

    std::vector<WindowQuadList> quads;
    quads.resize(ContentLeaf + 1);
    int last_content_id = id();

    // TODO: remove again once we are sure that content ids never repeat.
    auto content_ids = std::vector<int>{last_content_id};

    // Split the quads into separate lists for each type
    for (auto const& quad : data.quads) {
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
    if (data.crossFadeProgress() != 1.0) {
        auto previous = previousWindowPixmap<window_pixmap>();
        if (previous) {
            has_previous_content = true;
            quads.resize(quads.size() + 1);
            auto const& old_content_rect = previous->contentsRect();

            for (const WindowQuad& quad : quads[ContentLeaf]) {
                if (quad.id() != static_cast<int>(id())) {
                    // We currently only do this for the main window and not annexed children
                    // that means we can skip from here on.
                    break;
                }

                // we need to create new window quads with normalize texture coordinates
                // normal quads divide the x/y position by width/height. This would not work as the
                // texture is larger than the visible content in case of a decorated Client
                // resulting in garbage being shown. So we calculate the normalized texture
                // coordinate in the Client's new content space and map it to the previous Client's
                // content space.
                WindowQuad newQuad(WindowQuadContents);
                auto const content_geo = win::frame_relative_client_rect(toplevel);

                for (int i = 0; i < 4; ++i) {
                    auto const xFactor = (quad[i].textureX() - content_geo.x())
                        / static_cast<double>(content_geo.width());
                    auto const yFactor = (quad[i].textureY() - content_geo.y())
                        / static_cast<double>(content_geo.height());

                    // TODO(romangg): How can these be interpreted?
                    auto const old_x = xFactor * old_content_rect.width() + old_content_rect.x();
                    auto const old_y = yFactor * old_content_rect.height() + old_content_rect.y();

                    WindowVertex vertex(quad[i].x(),
                                        quad[i].y(),
                                        old_x / previous->size().width(),
                                        old_y / previous->size().height());
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
    GLVertex2D* map = (GLVertex2D*)vbo->map(size);

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
        scissorRegion = m_scene->mapToRenderTarget(region);
    }

    for (size_t i = 0; i < quads.size(); i++) {
        if (nodes[i].vertexCount == 0)
            continue;

        setBlendEnabled(nodes[i].hasAlpha || nodes[i].opacity < 1.0);

        if (opacity != nodes[i].opacity) {
            shader->setUniform(GLShader::ModulationConstant,
                               modulate(nodes[i].opacity, data.brightness()));
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

    if (!data.shader)
        ShaderManager::instance()->popShader();

    endRenderWindow();
}

//****************************************
// window_pixmap
//****************************************

window_pixmap::window_pixmap(render::window* window, gl::scene* scene)
    : render::window_pixmap(window)
    , m_texture(scene->createTexture())
    , m_scene(scene)
{
}

window_pixmap::~window_pixmap()
{
}

static bool needsPixmapUpdate(const window_pixmap* pixmap)
{
    // That's a regular Wayland client.
    if (pixmap->surface()) {
        return !pixmap->surface()->trackedDamage().isEmpty();
    }

    // That's an internal client with a raster buffer attached.
    if (!pixmap->internalImage().isNull()) {
        return !pixmap->toplevel()->damage().isEmpty();
    }

    // That's an internal client with an opengl framebuffer object attached.
    if (pixmap->fbo()) {
        return !pixmap->toplevel()->damage().isEmpty();
    }

    // That's an X11 client.
    return false;
}

render::gl::texture* window_pixmap::texture() const
{
    return m_texture.data();
}

bool window_pixmap::bind()
{
    if (!m_texture->isNull()) {
        if (!toplevel()->damage().isEmpty()) {
            updateBuffer();
        }
        if (needsPixmapUpdate(this)) {
            m_texture->updateFromPixmap(this);
            // mipmaps need to be updated
            m_texture->setDirty();
        }
        toplevel()->resetDamage();
        return true;
    }
    if (!isValid()) {
        return false;
    }

    bool success = m_texture->load(this);

    if (success) {
        toplevel()->resetDamage();
    } else {
        qCDebug(KWIN_CORE) << "Failed to bind window";
    }
    return success;
}

bool window_pixmap::isValid() const
{
    if (!m_texture->isNull()) {
        return true;
    }
    return render::window_pixmap::isValid();
}

}
