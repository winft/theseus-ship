/*
    SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QRegion>
#include <epoxy/gl.h>
#include <optional>
#include <span>

namespace KWin
{

namespace effect
{
struct render_data;
}

class GLVertexBufferPrivate;

enum VertexAttributeType {
    VA_Position = 0,
    VA_TexCoord = 1,
    VertexAttributeCount = 2,
};

/**
 * Describes the format of a vertex attribute stored in a buffer object.
 *
 * The attribute format consists of the attribute index, the number of
 * vector components, the data type, and the offset of the first element
 * relative to the start of the vertex data.
 */
struct GLVertexAttrib {
    int index;          /** The attribute index */
    int size;           /** The number of components [1..4] */
    GLenum type;        /** The type (e.g. GL_FLOAT) */
    int relativeOffset; /** The relative offset of the attribute */
};

/**
 * @short Vertex Buffer Object
 *
 * This is a short helper class to use vertex buffer objects (VBO). A VBO can be used to buffer
 * vertex data and to store them on graphics memory. It is the only allowed way to pass vertex
 * data to the GPU in OpenGL ES 2 and OpenGL 3 with forward compatible mode.
 *
 * If VBOs are not supported on the used OpenGL profile this class falls back to legacy
 * rendering using client arrays. Therefore this class should always be used for rendering
 * geometries.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.6
 */
class KWIN_EXPORT GLVertexBuffer
{
public:
    /**
     * Enum to define how often the vertex data in the buffer object changes.
     */
    enum UsageHint {
        Dynamic, ///< frequent changes, but used several times for rendering
        Static,  ///< No changes to data
        Stream   ///< Data only used once for rendering, updated very frequently
    };

    explicit GLVertexBuffer(UsageHint hint);
    ~GLVertexBuffer();

    /**
     * Specifies how interleaved vertex attributes are laid out in
     * the buffer object.
     *
     * Note that the attributes and the stride should be 32 bit aligned
     * or a performance penalty may be incurred.
     *
     * For some hardware the optimal stride is a multiple of 32 bytes.
     *
     * Example:
     *
     *     struct Vertex {
     *         QVector3D position;
     *         QVector2D texcoord;
     *     };
     *
     *     const GLVertexAttrib attribs[] = {
     *         { VA_Position, 3, GL_FLOAT, offsetof(Vertex, position) },
     *         { VA_TexCoord, 2, GL_FLOAT, offsetof(Vertex, texcoord) }
     *     };
     *
     *     Vertex vertices[6];
     *     vbo->setAttribLayout(attribs, 2, sizeof(Vertex));
     *     vbo->setData(vertices, sizeof(vertices));
     */
    void setAttribLayout(const GLVertexAttrib* attribs, int count, int stride);

    /**
     * Uploads data into the buffer object's data store.
     */
    void setData(const void* data, size_t sizeInBytes);

    /**
     * Sets the number of vertices that will be drawn by the render() method.
     */
    void setVertexCount(int count);

    /**
     * Sets the vertex data.
     * @param numberVertices The number of vertices in the arrays
     * @param dim The dimension of the vertices: 2 for x/y, 3 for x/y/z
     * @param vertices The vertices, size must equal @a numberVertices * @a dim
     * @param texcoords The texture coordinates for each vertex.
     * Size must equal 2 * @a numberVertices.
     */
    void setData(int numberVertices, int dim, const float* vertices, const float* texcoords);

    /**
     * Maps an unused range of the data store into the client's address space.
     *
     * The data store will be reallocated if it is smaller than the given size.
     *
     * The buffer object is mapped for writing, not reading. Attempts to read from
     * the mapped buffer range may result in system errors, including program
     * termination. The data in the mapped region is undefined until it has been
     * written to. If subsequent GL calls access unwritten memory, the results are
     * undefined and system errors, including program termination, may occur.
     *
     * No GL calls that access the buffer object must be made while the buffer
     * object is mapped. The returned pointer must not be passed as a parameter
     * value to any GL function.
     *
     * It is assumed that the GL_ARRAY_BUFFER_BINDING will not be changed while
     * the buffer object is mapped.
     */
    template<typename T>
    std::optional<std::span<T>> map(size_t count)
    {
        auto const void_map = map(sizeof(T) * count);
        if (!void_map) {
            return std::nullopt;
        }

        return std::span(reinterpret_cast<T*>(void_map), count);
    }

    /**
     * Flushes the mapped buffer range and unmaps the buffer.
     */
    void unmap();

    /**
     * Binds the vertex arrays to the context.
     */
    void bindArrays();

    /**
     * Disables the vertex arrays.
     */
    void unbindArrays();

    /**
     * Draws count vertices beginning with first.
     */
    void draw(GLenum primitiveMode, int first, int count);

    /**
     * Draws count vertices beginning with first.
     */
    void draw(effect::render_data const& data,
              QRegion const& region,
              GLenum primitiveMode,
              int first,
              int count);

    /**
     * Renders the vertex data in given @a primitiveMode.
     * Please refer to OpenGL documentation of glDrawArrays or glDrawElements for allowed
     * values for @a primitiveMode. Best is to use GL_TRIANGLES or similar to be future
     * compatible.
     */
    void render(GLenum primitiveMode);
    /**
     * Same as above restricting painting to @a region.
     */
    void render(effect::render_data const& data, const QRegion& region, GLenum primitiveMode);
    /**
     * Sets the color the geometry will be rendered with.
     * For legacy rendering glColor is used before rendering the geometry.
     * For core shader a uniform "geometryColor" is expected and is set.
     * @param color The color to render the geometry
     * @param enableColor Whether the geometry should be rendered with a color or not
     * @see setUseColor
     * @see isUseColor
     * @since 4.7
     */
    void setColor(const QColor& color, bool enableColor = true);
    /**
     * @return @c true if geometry will be painted with a color, @c false otherwise
     * @see setUseColor
     * @see setColor
     * @since 4.7
     */
    bool isUseColor() const;
    /**
     * Enables/Disables rendering the geometry with a color.
     * If no color is set an opaque, black color is used.
     * @param enable Enable/Disable rendering with color
     * @see isUseColor
     * @see setColor
     * @since 4.7
     */
    void setUseColor(bool enable);

    /**
     * Resets the instance to default values.
     * Useful for shared buffers.
     * @since 4.7
     */
    void reset();

    /**
     * Notifies the vertex buffer that we are done painting the frame.
     *
     * @internal
     */
    void endOfFrame();

    /**
     * Notifies the vertex buffer that we are about to paint a frame.
     *
     * @internal
     */
    void beginFrame();

    /**
     * @internal
     */
    static void initStatic();

    /**
     * @internal
     */
    static void cleanup();

    /**
     * Returns true if indexed quad mode is supported, and false otherwise.
     */
    static bool supportsIndexedQuads();

    /**
     * @return A shared VBO for streaming data
     * @since 4.7
     */
    static GLVertexBuffer* streamingBuffer();

private:
    GLvoid* map(size_t size);
    void draw_primitive(effect::render_data const& data,
                        QRegion const& region,
                        GLenum mode,
                        int first,
                        int count);
    void draw_primitive_unbounded(GLenum mode, int first, int count);

    void draw_primitive_quads(effect::render_data const& data,
                              QRegion const& region,
                              int first,
                              int count);
    void draw_primitive_quads_unbounded(int first, int count);
    void prepare_primitive_quads_buffer(int& count);

    GLVertexBufferPrivate* const d;
};

}
