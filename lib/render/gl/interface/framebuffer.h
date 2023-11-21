/*
    SPDX-FileCopyrightText: 2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <epoxy/gl.h>
#include <kwin_export.h>
#include <render/interface/framebuffer.h>

#include <QRect>
#include <QSize>

namespace KWin
{

namespace effect
{
struct render_data;
}

class GLTexture;

void cleanupGL();

/**
 * Framebuffer object enables you to render onto a texture. This texture can later be used to e.g.
 * do post-processing of the scene.
 */
class KWIN_EXPORT GLFramebuffer : public render::framebuffer
{
public:
    GLFramebuffer() = default;
    GLFramebuffer(GLFramebuffer&& other) noexcept;
    GLFramebuffer& operator=(GLFramebuffer&& other) noexcept;
    GLFramebuffer(GLuint framebuffer, QSize const& size, QRect const& viewport);
    explicit GLFramebuffer(GLTexture* texture);

    ~GLFramebuffer() override;

    QRect viewport() const;
    QSize size() const override;

    bool valid() const
    {
        return mValid;
    }

    static void initStatic();
    static bool supported()
    {
        return sSupported;
    }

    /**
     * Whether the GL_EXT_framebuffer_blit extension is supported.
     * This functionality is not available in OpenGL ES 2.0.
     *
     * @returns whether framebuffer blitting is supported.
     * @since 4.8
     */
    static bool blitSupported();

    /**
     * Blits from @a source rectangle in logical coordinates in the current framebuffer to the @a
     * destination rectangle in texture-local coordinates in this framebuffer, taking into account
     * any transformations the source render target may have.
     *
     * Be aware that framebuffer blitting may not be supported on all hardware. Use blitSupported()
     * to check whether it is supported.
     */
    bool blit_from_current_render_target(effect::render_data& data,
                                         QRect const& source,
                                         QRect const& destination);

    void bind() override;

    GLTexture* const texture{nullptr};

protected:
    void initFBO(GLTexture* texture);

private:
    friend void KWin::cleanupGL();
    static void cleanup();

    void blit_from_current_render_target_impl(effect::render_data& data,
                                              QRect const& source,
                                              QRect const& destination);

    static bool sSupported;
    static bool s_blitSupported;

    GLuint mFramebuffer{0};
    std::unique_ptr<GLTexture> blit_helper_tex;

    QSize mSize;
    QRect mViewport;
    bool mValid{false};
    bool mForeign{false};
};

}
