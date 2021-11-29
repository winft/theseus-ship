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
#include "scene.h"
#include "shadow.h"

#ifdef KWIN_HAVE_XRENDER_COMPOSITING

namespace KWin
{

namespace Xcb
{
class Shm;
}

namespace render::xrender
{

/**
 * @brief Backend for the scene to hold the compositing buffer and take care of buffer
 * swapping.
 *
 * This class is intended as a small abstraction to support multiple compositing backends in the
 * scene.
 */
class backend
{
public:
    virtual ~backend();
    virtual void present(int mask, const QRegion& damage) = 0;

    /**
     * @brief Returns the OverlayWindow used by the backend.
     *
     * A backend does not have to use an OverlayWindow, this is mostly for the X world.
     * In case the backend does not use an OverlayWindow it is allowed to return @c null.
     * It's the task of the caller to check whether it is @c null.
     *
     * @return :OverlayWindow*
     */
    virtual OverlayWindow* overlayWindow();
    virtual bool usesOverlayWindow() const = 0;
    /**
     * @brief Shows the Overlay Window
     *
     * Default implementation does nothing.
     */
    virtual void showOverlay();
    /**
     * @brief React on screen geometry changes.
     *
     * Default implementation does nothing. Override if specific functionality is required.
     *
     * @param size The new screen size
     */
    virtual void screenGeometryChanged(const QSize& size);
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

protected:
    backend();
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

private:
    // Create the compositing buffer. The root window is not double-buffered,
    // so it is done manually using this buffer,
    xcb_render_picture_t m_buffer;
    bool m_failed;
};

/**
 * @brief backend using an X11 Overlay Window as compositing target.
 */
class x11_overlay_backend : public backend
{
public:
    x11_overlay_backend();
    ~x11_overlay_backend() override;

    void present(int mask, const QRegion& damage) override;
    OverlayWindow* overlayWindow() override;
    void showOverlay() override;
    void screenGeometryChanged(const QSize& size) override;
    bool usesOverlayWindow() const override;

private:
    void init(bool createOverlay);
    void createBuffer();
    QScopedPointer<OverlayWindow> m_overlayWindow;
    xcb_render_picture_t m_front;
    xcb_render_pictformat_t m_format;
};

class scene : public render::scene
{
    Q_OBJECT
public:
    class EffectFrame;
    ~scene() override;
    bool initFailed() const override;
    CompositingType compositingType() const override
    {
        return XRenderCompositing;
    }
    qint64 paint(QRegion damage,
                 std::deque<Toplevel*> const& windows,
                 std::chrono::milliseconds presentTime) override;
    render::effect_frame* createEffectFrame(EffectFrameImpl* frame) override;
    Shadow* createShadow(Toplevel* toplevel) override;
    void screenGeometryChanged(const QSize& size) override;
    xcb_render_picture_t xrenderBufferPicture() const override;
    OverlayWindow* overlayWindow() const override
    {
        return m_backend->overlayWindow();
    }
    bool usesOverlayWindow() const override
    {
        return m_backend->usesOverlayWindow();
    }
    Decoration::Renderer*
    createDecorationRenderer(Decoration::DecoratedClientImpl* client) override;

    bool animationsSupported() const override
    {
        return true;
    }

    static scene* createScene(QObject* parent);

    static ScreenPaintData screen_paint;

protected:
    render::window* createWindow(Toplevel* toplevel) override;
    void paintBackground(QRegion region) override;
    void paintGenericScreen(int mask, ScreenPaintData data) override;
    void paintDesktop(int desktop, int mask, const QRegion& region, ScreenPaintData& data) override;
    void paintCursor() override;
    void paintEffectQuickView(EffectQuickView* w) override;

private:
    explicit scene(xrender::backend* backend, QObject* parent = nullptr);
    QScopedPointer<xrender::backend> m_backend;
};

class window : public render::window
{
public:
    window(Toplevel* c, xrender::scene* scene);
    ~window() override;
    void performPaint(int mask, QRegion region, WindowPaintData data) override;
    QRegion transformedShape() const;
    void setTransformedShape(const QRegion& shape);
    static void cleanup();

protected:
    render::window_pixmap* createWindowPixmap() override;

private:
    QRect mapToScreen(int mask, const WindowPaintData& data, const QRect& rect) const;
    QPoint mapToScreen(int mask, const WindowPaintData& data, const QPoint& point) const;
    QRect bufferToWindowRect(const QRect& rect) const;
    QRegion bufferToWindowRegion(const QRegion& region) const;
    void prepareTempPixmap();
    void setPictureFilter(xcb_render_picture_t pic, image_filter_type filter);
    xrender::scene* m_scene;
    xcb_render_pictformat_t format;
    QRegion transformed_shape;
    static QRect temp_visibleRect;
    static XRenderPicture* s_tempPicture;
    static XRenderPicture* s_fadeAlphaPicture;
};

class window_pixmap : public render::window_pixmap
{
public:
    explicit window_pixmap(render::window* window, xcb_render_pictformat_t format);
    ~window_pixmap() override;
    xcb_render_picture_t picture() const;
    void create() override;

private:
    xcb_render_picture_t m_picture;
    xcb_render_pictformat_t m_format;
};

class effect_frame : public render::effect_frame
{
public:
    effect_frame(EffectFrameImpl* frame);
    ~effect_frame() override;

    void free() override;
    void freeIconFrame() override;
    void freeTextFrame() override;
    void freeSelection() override;
    void crossFadeIcon() override;
    void crossFadeText() override;
    void render(QRegion region, double opacity, double frameOpacity) override;
    static void cleanup();

private:
    void updatePicture();
    void updateTextPicture();
    void renderUnstyled(xcb_render_picture_t pict, const QRect& rect, qreal opacity);

    XRenderPicture* m_picture;
    XRenderPicture* m_textPicture;
    XRenderPicture* m_iconPicture;
    XRenderPicture* m_selectionPicture;
    static XRenderPicture* s_effectFrameCircle;
};

inline xcb_render_picture_t scene::xrenderBufferPicture() const
{
    return m_backend->buffer();
}

inline QRegion window::transformedShape() const
{
    return transformed_shape;
}

inline void window::setTransformedShape(const QRegion& shape)
{
    transformed_shape = shape;
}

inline xcb_render_picture_t window_pixmap::picture() const
{
    return m_picture;
}

/**
 * @short XRender implementation of Shadow.
 *
 * This class extends Shadow by the elements required for XRender rendering.
 * @author Jacopo De Simoi <wilderkde@gmail.org>
 */
class shadow : public KWin::Shadow
{
public:
    explicit shadow(Toplevel* toplevel);
    using Shadow::ShadowElementBottom;
    using Shadow::ShadowElementBottomLeft;
    using Shadow::ShadowElementBottomRight;
    using Shadow::ShadowElementLeft;
    using Shadow::ShadowElementRight;
    using Shadow::ShadowElements;
    using Shadow::ShadowElementsCount;
    using Shadow::ShadowElementTop;
    using Shadow::ShadowElementTopLeft;
    using Shadow::ShadowElementTopRight;
    using Shadow::shadowPixmap;
    ~shadow() override;

    void layoutShadowRects(QRect& top,
                           QRect& topRight,
                           QRect& right,
                           QRect& bottomRight,
                           QRect& bottom,
                           QRect& bottomLeft,
                           QRect& Left,
                           QRect& topLeft);
    xcb_render_picture_t picture(ShadowElements element) const;

protected:
    void buildQuads() override;
    bool prepareBackend() override;

private:
    XRenderPicture* m_pictures[ShadowElementsCount];
};

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

class KWIN_EXPORT scene_factory : public render::scene_factory
{
    Q_OBJECT
    Q_INTERFACES(KWin::render::scene_factory)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Scene" FILE "xrender.json")

public:
    explicit scene_factory(QObject* parent = nullptr);
    ~scene_factory() override;

    render::scene* create(QObject* parent = nullptr) const override;
};

}
}

#endif
