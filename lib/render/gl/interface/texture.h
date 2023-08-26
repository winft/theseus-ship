/*
    SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>
#include <render/effect/interface/paint_data.h>
#include <render/effect/interface/types.h>

#include <QMatrix4x4>
#include <QRegion>
#include <QSharedPointer>
#include <QSize>

#include <epoxy/gl.h>

class QImage;
class QPixmap;

namespace KWin
{

class GLVertexBuffer;
class GLTexturePrivate;

enum TextureCoordinateType { NormalizedCoordinates = 0, UnnormalizedCoordinates };

class KWIN_EXPORT GLTexture
{
public:
    GLTexture();
    GLTexture(GLTexture&& tex);
    explicit GLTexture(const QImage& image, GLenum target = GL_TEXTURE_2D);
    explicit GLTexture(const QPixmap& pixmap, GLenum target = GL_TEXTURE_2D);
    explicit GLTexture(const QString& fileName);
    GLTexture(GLenum internalFormat, int width, int height, int levels = 1);
    GLTexture(GLenum internalFormat, QSize const& size, int levels = 1);

    /**
     * Create a GLTexture wrapper around an existing texture.
     * Management of the underlying texture remains the responsibility of the caller.
     * @since 5.18
     */
    explicit GLTexture(GLuint textureId, GLenum internalFormat, const QSize& size, int levels = 1);
    virtual ~GLTexture();

    GLTexture& operator=(GLTexture&& tex);

    bool isNull() const;
    QSize size() const;
    int width() const;
    int height() const;

    /**
     * sets the transform between the content and the buffer
     */
    void set_content_transform(effect::transform_type transform);

    /**
     * @returns the transform between the content and the buffer
     */
    effect::transform_type get_content_transform() const;

    /**
     * @returns the transform between the content and the buffer as a matrix
     */
    QMatrix4x4 get_content_transform_matrix() const;

    /**
     * Specifies which component of a texel is placed in each respective
     * component of the vector returned to the shader.
     *
     * Valid values are GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA, GL_ONE and GL_ZERO.
     *
     * @see swizzleSupported()
     * @since 5.2
     */
    void setSwizzle(GLenum red, GLenum green, GLenum blue, GLenum alpha);

    /**
     * Returns a matrix that transforms texture coordinates of the given type,
     * taking the texture target and the y-inversion flag into account.
     *
     * @since 4.11
     */
    QMatrix4x4 matrix(TextureCoordinateType type) const;

    void
    update(const QImage& image, const QPoint& offset = QPoint(0, 0), const QRect& src = QRect());
    virtual void discard();
    void bind();
    void unbind();

    void render(QSize const& target_size);
    void render(effect::render_data const& data, QRegion const& region, QSize const& target_size);
    void render(effect::render_data const& data,
                QRect const& source,
                QRegion const& region,
                QSize const& target_size);

    GLuint texture() const;
    GLenum target() const;
    GLenum filter() const;
    GLenum internalFormat() const;

    QImage toImage() const;

    /** @short
     * Make the texture fully transparent
     */
    void clear();
    /**
     * @deprecated track modifications to the texture yourself
     */
    void setDirty();
    bool isDirty() const;
    void setFilter(GLenum filter);
    void setWrapMode(GLenum mode);

    void generateMipmaps();

    static bool framebufferObjectSupported();

    /**
     * Returns true if texture swizzle is supported, and false otherwise
     *
     * Texture swizzle requires OpenGL 3.3, GL_ARB_texture_swizzle, or OpenGL ES 3.0.
     *
     * @since 5.2
     */
    static bool supportsSwizzle();

    /**
     * Returns @c true if texture formats R* are supported, and @c false otherwise.
     *
     * This requires OpenGL 3.0, GL_ARB_texture_rg or OpenGL ES 3.0 or GL_EXT_texture_rg.
     *
     * @since 5.2.1
     */
    static bool supportsFormatRG();

protected:
    std::unique_ptr<GLTexturePrivate> d_ptr;
    GLTexture(std::unique_ptr<GLTexturePrivate> impl);

private:
    Q_DECLARE_PRIVATE(GLTexture)
};

}
