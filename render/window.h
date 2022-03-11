/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "shadow.h"
#include "types.h"

#include <kwineffects/paint_data.h>

#include <memory>

class QOpenGLFramebufferObject;

namespace Wrapland::Server
{
class Buffer;
class Surface;
}

namespace KWin
{

class Toplevel;

namespace render
{

class buffer;
class effects_window_impl;

class KWIN_EXPORT window
{
public:
    window(Toplevel* c);
    virtual ~window();
    uint32_t id() const;

    // perform the actual painting of the window
    virtual void performPaint(paint_type mask, QRegion region, WindowPaintData data) = 0;

    // do any cleanup needed when the window's buffer is discarded
    void discard_buffer();
    void update_buffer();

    int x() const;
    int y() const;
    int width() const;
    int height() const;
    QRect geometry() const;
    QPoint pos() const;
    QSize size() const;
    QRect rect() const;

    // access to the internal window class
    // TODO eventually get rid of this
    Toplevel* get_window() const;

    // should the window be painted
    bool isPaintingEnabled() const;
    void resetPaintingEnabled();
    void enablePainting(window_paint_disable_type reason);
    void disablePainting(window_paint_disable_type reason);

    // is the window visible at all
    bool isVisible() const;

    // is the window fully opaque
    bool isOpaque() const;
    QRegion decorationShape() const;
    QPoint bufferOffset() const;
    void updateToplevel(Toplevel* c);

    // creates initial quad list for the window
    WindowQuadList buildQuads(bool force = false) const;

    void create_shadow();
    void updateShadow(render::shadow* shadow);
    render::shadow const* shadow() const;
    render::shadow* shadow();

    void reference_previous_buffer();
    void unreference_previous_buffer();
    void invalidateQuadsCache();

    std::unique_ptr<effects_window_impl> effect;
    shadow_windowing_integration shadow_windowing;

    std::function<void(Toplevel*, std::shared_ptr<Wrapland::Server::Buffer>&)>
        update_wayland_buffer;
    std::function<QRectF(Toplevel*, QRectF const&)> get_wayland_viewport;

protected:
    WindowQuadList
    makeDecorationQuads(const QRect* rects, const QRegion& region, qreal textureScale = 1.0) const;
    WindowQuadList makeContentsQuads(int id, QPoint const& offset = QPoint()) const;

    /**
     * @brief Returns the buffer for this Window.
     *
     * If the buffer does not yet exist, this method will invoke create_buffer.
     * If the buffer is not valid it tries to create it, in case this succeeds the
     * buffer is returned. In case it fails, the previous (and still valid) buffer is
     * returned.
     *
     * @note This method can return @c NULL as there might neither be a valid previous nor current
     * buffer around.
     *
     * The buffer gets casted to the type passed in as a template parameter. That way this
     * class does not need to know the actual buffer subclass used by the concrete scene
     * implementations.
     *
     * @return The buffer casted to T* or @c NULL if there is no valid buffer.
     */
    template<typename T>
    T* get_buffer();
    template<typename T>
    T* previous_buffer();

    /**
     * @brief Factory method to create a buffer.
     *
     * The inheriting classes need to implement this method to create a new instance of their
     * buffer subclass.
     * @note Do not use buffer::create on the created instance. The scene will take care of
     * that.
     */
    virtual buffer* create_buffer() = 0;
    Toplevel* toplevel;
    image_filter_type filter;
    render::shadow* m_shadow;

private:
    struct {
        std::unique_ptr<buffer> current;
        std::unique_ptr<buffer> previous;
        int previous_refs{0};
    } buffers;
    window_paint_disable_type disable_painting{window_paint_disable_type::none};
    mutable std::unique_ptr<WindowQuadList> cached_quad_list;
    uint32_t const m_id;
    Q_DISABLE_COPY(window)
};

/**
 * @brief Wrapper for a buffer of the window.
 *
 * This class encapsulates the functionality to get the buffer for a window. When initialized the
 * buffer is not yet mapped to the window and isValid will return @c false. The buffer mapping to
 * the window can be established through @ref create. If it succeeds isValid will return @c true,
 * otherwise it will keep in the non valid state and it can be tried to create the buffer mapping
 * again (e.g. in the next frame).
 *
 * This class is not intended to be updated when the buffer is no longer valid due to e.g. resizing
 * the window. Instead a new instance of this class should be instantiated. The idea behind this is
 * that a valid buffer does not get destroyed, but can continue to be used. To indicate that a newer
 * buffer should in generally be around, one can use markAsDiscarded.
 *
 * This class is intended to be inherited for the needs of the compositor backends which need
 * further mapping from the native buffer to the respective rendering format.
 */
class KWIN_EXPORT buffer
{
public:
    virtual ~buffer();
    /**
     * @brief Tries to create the mapping between the window and the buffer.
     *
     * In case this method succeeds in creating the buffer for the window, isValid will return @c
     * true otherwise @c false.
     *
     * Inheriting classes should re-implement this method in case they need to add further
     * functionality for mapping the native buffer to the rendering format.
     */
    virtual void create();

    /**
     * @return @c true if the buffer has been created and is valid, @c false otherwise
     */
    virtual bool isValid() const;

    /**
     * @return The native X11 pixmap handle
     */
    xcb_pixmap_t pixmap() const;

    /**
     * @return The Wayland Buffer for this buffer.
     */
    Wrapland::Server::Buffer* wayland_buffer() const;
    std::shared_ptr<QOpenGLFramebufferObject> const& fbo() const;
    QImage internalImage() const;

    /**
     * @brief Whether this buffer is considered as discarded. This means the window has
     * changed in a way that a new buffer should have been created already.
     *
     * @return @c true if this buffer is considered as discarded, @c false otherwise.
     * @see markAsDiscarded
     */
    bool isDiscarded() const;

    /**
     * @brief Marks this buffer as discarded. From now on isDiscarded will return @c true.
     * This method should only be used by the Window when it changes in a way that a new buffer is
     * required.
     *
     * @see isDiscarded
     */
    void markAsDiscarded();

    /**
     * The size of the buffer.
     */
    const QSize& size() const;

    /**
     * The geometry of the Client's content inside the buffer. In case of a decorated Client the
     * buffer may also contain the decoration, which is not rendered into this buffer though. This
     * contentsRect tells where inside the complete buffer the real content is.
     */
    const QRect& contentsRect() const;

    /**
     * @brief Returns the Toplevel this buffer belongs to.
     * Note: the Toplevel can change over the lifetime of the buffer in case the Toplevel is
     * copied to Deleted.
     */
    Toplevel* toplevel() const;

    /**
     * @returns the surface this buffer references, might be @c null.
     */
    Wrapland::Server::Surface* surface() const;

protected:
    explicit buffer(render::window* window);

    /**
     * Should be called by the implementing subclasses when the Wayland Buffer changed and needs
     * updating.
     */
    virtual void updateBuffer();

private:
    render::window* m_window;
    xcb_pixmap_t m_pixmap;
    QSize m_pixmapSize;
    bool m_discarded;
    QRect m_contentsRect;
    std::shared_ptr<Wrapland::Server::Buffer> m_buffer;
    std::shared_ptr<QOpenGLFramebufferObject> m_fbo;
    QImage m_internalImage;
};

template<typename T>
inline T* window::get_buffer()
{
    if (!buffers.current) {
        buffers.current.reset(create_buffer());
    }
    if (buffers.current->isValid()) {
        return static_cast<T*>(buffers.current.get());
    }
    buffers.current->create();
    if (buffers.current->isValid()) {
        return static_cast<T*>(buffers.current.get());
    } else {
        return static_cast<T*>(buffers.previous.get());
    }
}

template<typename T>
inline T* window::previous_buffer()
{
    return static_cast<T*>(buffers.previous.get());
}

}
}
