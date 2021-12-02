/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

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
#pragma once

#include "decorations/decorationrenderer.h"
#include "render/scene.h"

#include <memory>

namespace KWin::render
{

class compositor;

namespace x11
{
class overlay_window;
}

namespace xrender
{

/**
 * @brief Backend for the scene to hold the compositing buffer and take care of buffer
 * swapping. Using an X11 Overlay Window as compositing target.
 */
class backend
{
public:
    backend(render::compositor* compositor);
    ~backend();

    void present(paint_type mask, const QRegion& damage);

    /**
     * @brief Shows the Overlay Window
     *
     * Default implementation does nothing.
     */
    void showOverlay();

    /**
     * @brief React on screen geometry changes.
     *
     * Default implementation does nothing. Override if specific functionality is required.
     *
     * @param size The new screen size
     */
    void screenGeometryChanged(const QSize& size);

    /**
     * @brief The compositing buffer hold by this backend.
     *
     * The scene composites the new frame into this buffer.
     *
     * @return xcb_render_picture_t
     */
    xcb_render_picture_t buffer() const
    {
        return m_buffer;
    }

    /**
     * @brief Whether the creation of the Backend failed.
     *
     * The scene should test whether the Backend got constructed correctly. If this method
     * returns @c true, the scene should not try to start the rendering.
     *
     * @return bool @c true if the creation of the Backend failed, @c false otherwise.
     */
    bool isFailed() const
    {
        return m_failed;
    }

    std::unique_ptr<x11::overlay_window> overlay_window;

private:
    /**
     * @brief A subclass needs to call this method once it created the compositing back buffer.
     *
     * @param buffer The buffer to use for compositing
     * @return void
     */
    void setBuffer(xcb_render_picture_t buffer);
    /**
     * @brief Sets the backend initialization to failed.
     *
     * This method should be called by the concrete subclass in case the initialization failed.
     * The given @p reason is logged as a warning.
     *
     * @param reason The reason why the initialization failed.
     */
    void setFailed(const QString& reason);

    void init(bool createOverlay);
    void createBuffer();

    // Create the compositing buffer. The root window is not double-buffered,
    // so it is done manually using this buffer,
    xcb_render_picture_t m_buffer{XCB_RENDER_PICTURE_NONE};
    bool m_failed{false};

    xcb_render_picture_t m_front{XCB_RENDER_PICTURE_NONE};
    xcb_render_pictformat_t m_format{0};
};

class scene : public render::scene
{
    Q_OBJECT
public:
    scene(xrender::backend* backend);
    ~scene() override;

    bool initFailed() const override;
    CompositingType compositingType() const override
    {
        return XRenderCompositing;
    }
    int64_t paint(QRegion damage,
                  std::deque<Toplevel*> const& windows,
                  std::chrono::milliseconds presentTime) override;
    render::effect_frame* createEffectFrame(effect_frame_impl* frame) override;
    render::shadow* createShadow(Toplevel* toplevel) override;
    void handle_screen_geometry_change(QSize const& size) override;
    xcb_render_picture_t xrenderBufferPicture() const override;
    Decoration::Renderer*
    createDecorationRenderer(Decoration::DecoratedClientImpl* client) override;

    bool animationsSupported() const override
    {
        return true;
    }

    static ScreenPaintData screen_paint;

protected:
    render::window* createWindow(Toplevel* toplevel) override;
    void paintBackground(QRegion region) override;
    void paintGenericScreen(paint_type mask, ScreenPaintData data) override;
    void paintDesktop(int desktop,
                      paint_type mask,
                      const QRegion& region,
                      ScreenPaintData& data) override;
    void paintCursor() override;
    void paintEffectQuickView(EffectQuickView* w) override;

private:
    QScopedPointer<xrender::backend> m_backend;
};

inline xcb_render_picture_t scene::xrenderBufferPicture() const
{
    return m_backend->buffer();
}

class deco_renderer : public Decoration::Renderer
{
    Q_OBJECT
public:
    enum class DecorationPart : int { Left, Top, Right, Bottom, Count };
    explicit deco_renderer(Decoration::DecoratedClientImpl* client);
    ~deco_renderer() override;

    void render() override;
    void reparent(Toplevel* window) override;

    xcb_render_picture_t picture(DecorationPart part) const;

private:
    void resizePixmaps();
    QSize m_sizes[int(DecorationPart::Count)];
    xcb_pixmap_t m_pixmaps[int(DecorationPart::Count)];
    xcb_gcontext_t m_gc;
    XRenderPicture* m_pictures[int(DecorationPart::Count)];
};

KWIN_EXPORT render::scene* create_scene(render::compositor* compositor);

}
}
