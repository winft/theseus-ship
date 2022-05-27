/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/types.h"

#include <QRegion>
#include <QSize>
#include <QString>
#include <memory>
#include <xcb/render.h>

namespace KWin::render
{

namespace x11
{
class compositor;
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
    explicit backend(x11::compositor& compositor);
    ~backend();

    void present(paint_type mask, QRegion const& damage);

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
    void screenGeometryChanged(QSize const& size);

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
    void setFailed(QString const& reason);

    void init(bool createOverlay);
    void createBuffer();

    // Create the compositing buffer. The root window is not double-buffered,
    // so it is done manually using this buffer,
    xcb_render_picture_t m_buffer{XCB_RENDER_PICTURE_NONE};
    bool m_failed{false};

    xcb_render_picture_t m_front{XCB_RENDER_PICTURE_NONE};
    xcb_render_pictformat_t m_format{0};
};

}
}
