/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "types.h"

#include <QImage>
#include <memory>
#include <xcb/xcb.h>

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

class window;

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

}
}
