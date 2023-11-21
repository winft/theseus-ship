/*
    SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "framebuffer.h"

#include <base/logging.h>
#include <render/effect/interface/paint_data.h>
#include <render/effect/interface/types.h>
#include <render/gl/interface/platform.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/texture.h>
#include <render/gl/interface/utils.h>

#define DEBUG_GLFRAMEBUFFER 0

namespace KWin
{

/***  GLFramebuffer  ***/
bool GLFramebuffer::sSupported = false;
bool GLFramebuffer::s_blitSupported = false;

void GLFramebuffer::initStatic()
{
    if (GLPlatform::instance()->isGLES()) {
        sSupported = true;
        s_blitSupported = hasGLVersion(3, 0);
    } else {
        sSupported = hasGLVersion(3, 0)
            || hasGLExtension(QByteArrayLiteral("GL_ARB_framebuffer_object"))
            || hasGLExtension(QByteArrayLiteral("GL_EXT_framebuffer_object"));

        s_blitSupported = hasGLVersion(3, 0)
            || hasGLExtension(QByteArrayLiteral("GL_ARB_framebuffer_object"))
            || hasGLExtension(QByteArrayLiteral("GL_EXT_framebuffer_blit"));
    }
}

void GLFramebuffer::cleanup()
{
    sSupported = false;
    s_blitSupported = false;
}

bool GLFramebuffer::blitSupported()
{
    return s_blitSupported;
}

GLFramebuffer::GLFramebuffer(GLuint framebuffer, QSize const& size, QRect const& viewport)
    : mFramebuffer{framebuffer}
    , mSize{size}
    , mViewport{viewport}
    , mValid{true}
    , mForeign{true}
{
}

GLFramebuffer::GLFramebuffer(GLTexture* texture)
    : texture{texture}
    , mSize{texture->size()}
    , mViewport{QRect(QPoint(0, 0), mSize)}
{
    // Make sure FBO is supported
    if (sSupported && !texture->isNull()) {
        initFBO(texture);
    } else
        qCCritical(KWIN_CORE) << "Render targets aren't supported!";
}

GLFramebuffer::GLFramebuffer(GLFramebuffer&& other) noexcept
{
    *this = std::move(other);
}

GLFramebuffer& GLFramebuffer::operator=(GLFramebuffer&& other) noexcept
{
    mFramebuffer = other.mFramebuffer;
    mSize = other.mSize;
    mViewport = other.mViewport;
    mValid = other.mValid;
    mForeign = other.mForeign;

    other.mValid = false;
    return *this;
}

GLFramebuffer::~GLFramebuffer()
{
    if (mValid && !mForeign) {
        glDeleteFramebuffers(1, &mFramebuffer);
    }
}

QRect GLFramebuffer::viewport() const
{
    return mViewport;
}

QSize GLFramebuffer::size() const
{
    return mSize;
}

void GLFramebuffer::bind()
{
    if (!valid()) {
        qCCritical(KWIN_CORE) << "Can't enable invalid render target!";
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glViewport(mViewport.x(), mViewport.y(), mViewport.width(), mViewport.height());

    glEnable(GL_SCISSOR_TEST);
    glScissor(mViewport.x(), mViewport.y(), mViewport.width(), mViewport.height());
}

static QString formatFramebufferStatus(GLenum status)
{
    switch (status) {
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        // An attachment is the wrong type / is invalid / has 0 width or height
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        // There are no images attached to the framebuffer
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
    case GL_FRAMEBUFFER_UNSUPPORTED:
        // A format or the combination of formats of the attachments is unsupported
        return QStringLiteral("GL_FRAMEBUFFER_UNSUPPORTED");
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        // Not all attached images have the same width and height
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT");
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
        // The color attachments don't have the same format
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT");
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT:
        // The attachments don't have the same number of samples
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE");
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
        // The draw buffer is missing
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER");
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
        // The read buffer is missing
        return QStringLiteral("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER");
    default:
        return QStringLiteral("Unknown (0x") + QString::number(status, 16) + QStringLiteral(")");
    }
}

void GLFramebuffer::initFBO(GLTexture* texture)
{
    assert(texture);
    assert(!mForeign);

    GLint cur_fbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur_fbo);

#if DEBUG_GLFRAMEBUFFER
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        qCCritical(KWIN_CORE) << "Error status when entering GLFramebuffer::initFBO: "
                              << formatGLError(err);
#endif

    glGenFramebuffers(1, &mFramebuffer);

#if DEBUG_GLFRAMEBUFFER
    if ((err = glGetError()) != GL_NO_ERROR) {
        qCCritical(KWIN_CORE) << "glGenFramebuffers failed: " << formatGLError(err);
        return;
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);

#if DEBUG_GLFRAMEBUFFER
    if ((err = glGetError()) != GL_NO_ERROR) {
        qCCritical(KWIN_CORE) << "glBindFramebuffer failed: " << formatGLError(err);
        glDeleteFramebuffers(1, &mFramebuffer);
        return;
    }
#endif

    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture->target(), texture->texture(), 0);

#if DEBUG_GLFRAMEBUFFER
    if ((err = glGetError()) != GL_NO_ERROR) {
        qCCritical(KWIN_CORE) << "glFramebufferTexture2D failed: " << formatGLError(err);
        glBindFramebuffer(GL_FRAMEBUFFER, cur_fbo);
        glDeleteFramebuffers(1, &mFramebuffer);
        return;
    }
#endif

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    glBindFramebuffer(GL_FRAMEBUFFER, cur_fbo);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        // We have an incomplete framebuffer, consider it invalid
        if (status == 0)
            qCCritical(KWIN_CORE) << "glCheckFramebufferStatus failed: "
                                  << formatGLError(glGetError());
        else
            qCCritical(KWIN_CORE) << "Invalid framebuffer status: "
                                  << formatFramebufferStatus(status);
        glDeleteFramebuffers(1, &mFramebuffer);
        return;
    }

    mValid = true;
}

void GLFramebuffer::blit_from_current_render_target_impl(effect::render_data& data,
                                                         QRect const& source,
                                                         QRect const& destination)
{
    auto top = static_cast<GLFramebuffer*>(data.targets.top());

    render::push_framebuffer(data, this);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFramebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, top->mFramebuffer);

    auto const s = source.isNull() ? QRect(QPoint(0, 0), top->size())
                                   : effect::map_to_viewport(data, source);
    auto const d = destination.isNull() ? QRect(QPoint(0, 0), size()) : destination;

    GLuint srcX0 = s.x();
    GLuint srcY0 = s.y();
    GLuint srcX1 = s.x() + s.width();
    GLuint srcY1 = s.height() + s.y();

    const GLuint dstX0 = d.x();
    const GLuint dstY0 = size().height() - (d.y() + d.height());
    const GLuint dstX1 = d.x() + d.width();
    const GLuint dstY1 = size().height() - d.y();

    glBlitFramebuffer(
        srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    render::pop_framebuffer(data);
}

bool GLFramebuffer::blit_from_current_render_target(effect::render_data& data,
                                                    QRect const& source,
                                                    QRect const& destination)
{
    using tt = effect::transform_type;
    auto const has_rotation = data.transform == tt::rotated_90 || data.transform == tt::rotated_270
        || data.transform == tt::flipped_90 || data.transform == tt::flipped_270;

    if (!valid()) {
        qCWarning(KWIN_CORE) << "Draw fbo not valid. Abort blit from framebuffer.";
        return false;
    }

    if (data.targets.empty()) {
        qCWarning(KWIN_CORE) << "Abort blit from framebuffer due to no current render target.";
        return false;
    }
    if (!blitSupported()) {
        qCWarning(KWIN_CORE) << "Blitting not supported. Abort blit from framebuffer.";
        return false;
    }

    if (!has_rotation) {
        blit_from_current_render_target_impl(data, source, destination);
        return true;
    }

    auto top = data.targets.top();
    if (!blit_helper_tex || blit_helper_tex->width() < top->size().width()
        || blit_helper_tex->height() < top->size().height()) {
        blit_helper_tex = std::make_unique<GLTexture>(
            texture ? texture->internalFormat() : GL_RGBA8, top->size());
    }

    GLFramebuffer helper_fbo(blit_helper_tex.get());

    auto inter_rect = source.isNull() ? QRect(QPoint(0, 0), top->size())
                                      : effect::map_to_viewport(data, source);

    helper_fbo.blit_from_current_render_target_impl(data, source, inter_rect);

    render::push_framebuffer(data, this);

    QMatrix4x4 mat;
    mat.ortho(QRectF({}, size()));
    mat = effect::get_transform_matrix(data.transform) * mat;

    // GLTexture::render renders with origin (0, 0), move it to the correct place
    mat.translate(destination.x(), destination.y());

    ShaderBinder binder(ShaderTrait::MapTexture);
    binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, mat);

    blit_helper_tex->bind();
    blit_helper_tex->render(data, inter_rect, infiniteRegion(), destination.size());
    blit_helper_tex->unbind();

    render::pop_framebuffer(data);
    return true;
}

}
