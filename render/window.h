/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"
#include "shadow.h"
#include "types.h"

#include <kwineffects/paint_data.h>

#include <functional>
#include <memory>

namespace Wrapland::Server
{
class Buffer;
}

namespace KWin
{

class Toplevel;

namespace render
{

struct window_win_integration {
    std::function<void(buffer&)> setup_buffer;
    std::function<QRectF(Toplevel*, QRectF const&)> get_viewport;
};

class effects_window_impl;
class scene;

class KWIN_EXPORT window
{
public:
    window(Toplevel* c, render::scene& scene);
    virtual ~window();
    uint32_t id() const;

    // perform the actual painting of the window
    virtual void performPaint(paint_type mask, QRegion region, WindowPaintData data) = 0;

    // do any cleanup needed when the window's buffer is discarded
    void discard_buffer();
    void update_buffer();

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
    void updateShadow(std::unique_ptr<render::shadow> shadow);
    render::shadow const* shadow() const;
    render::shadow* shadow();

    void reference_previous_buffer();
    void unreference_previous_buffer();
    void invalidateQuadsCache();

    std::unique_ptr<effects_window_impl> effect;
    window_win_integration win_integration;
    shadow_windowing_integration shadow_windowing;
    render::scene& scene;

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
    std::unique_ptr<render::shadow> m_shadow;

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

template<typename T>
inline T* window::get_buffer()
{
    update_buffer();
    if (buffers.current->isValid()) {
        return static_cast<T*>(buffers.current.get());
    }
    return static_cast<T*>(buffers.previous.get());
}

template<typename T>
inline T* window::previous_buffer()
{
    return static_cast<T*>(buffers.previous.get());
}

}
}
