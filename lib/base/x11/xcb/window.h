/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include <QRect>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

/**
 * This class is an RAII wrapper for an xcb_window_t. An xcb_window_t hold by an instance of this
 * class will be freed when the instance gets destroyed.
 *
 * Furthermore the class provides wrappers around some xcb methods operating on an xcb_window_t.
 *
 * For the cases that one is more interested in wrapping the xcb methods the constructor which takes
 * an existing window and the @ref reset method allow to disable the RAII functionality.
 */
class window
{
public:
    window() = default;
    /**
     * Takes over responsibility of @p win. If @p win is not provided an invalid window is
     * created. Use @ref create to set an xcb_window_t later on.
     *
     * If @p destroy is @c true the window will be destroyed together with this object, if @c false
     * the window will be kept around. This is useful if you are not interested in the RAII
     * capabilities but still want to use a window like an object.
     *
     * @param win The window to manage.
     * @param destroy Whether the window should be destroyed together with the object.
     * @see reset
     */
    window(xcb_connection_t* con, xcb_window_t win, bool destroy = true);
    /**
     * Creates an xcb_window_t and manages it. It's a convenient method to create a window with
     * depth, class and visual being copied from parent and border being @c 0.
     * @param geometry The geometry for the window to be created
     * @param mask The mask for the values
     * @param values The values to be passed to xcb_create_window
     * @param parent The parent window
     */
    window(xcb_connection_t* con,
           xcb_window_t parent,
           QRect const& geometry,
           uint32_t mask = 0,
           const uint32_t* values = nullptr);
    /**
     * Creates an xcb_window_t and manages it. It's a convenient method to create a window with
     * depth and visual being copied from parent and border being @c 0.
     * @param geometry The geometry for the window to be created
     * @param windowClass The window class
     * @param mask The mask for the values
     * @param values The values to be passed to xcb_create_window
     * @param parent The parent window
     */
    window(xcb_connection_t* con,
           xcb_window_t parent,
           QRect const& geometry,
           uint16_t windowClass,
           uint32_t mask = 0,
           const uint32_t* values = nullptr);
    window(const window& other) = delete;
    ~window();

    /**
     * Creates a new window for which the responsibility is taken over. If a window had been managed
     * before it is freed.
     *
     * Depth, class and visual are being copied from parent and border is @c 0.
     * @param geometry The geometry for the window to be created
     * @param mask The mask for the values
     * @param values The values to be passed to xcb_create_window
     * @param parent The parent window
     */
    void create(xcb_connection_t* con,
                xcb_window_t parent,
                QRect const& geometry,
                uint32_t mask = 0,
                const uint32_t* values = nullptr);
    /**
     * Creates a new window for which the responsibility is taken over. If a window had been managed
     * before it is freed.
     *
     * Depth and visual are being copied from parent and border is @c 0.
     * @param geometry The geometry for the window to be created
     * @param windowClass The window class
     * @param mask The mask for the values
     * @param values The values to be passed to xcb_create_window
     * @param parent The parent window
     */
    void create(xcb_connection_t* con,
                xcb_window_t parent,
                QRect const& geometry,
                uint16_t windowClass,
                uint32_t mask = 0,
                const uint32_t* values = nullptr);
    /**
     * Frees the existing window and starts to manage the new @p win.
     * If @p destroy is @c true the new managed window will be destroyed together with this
     * object or when reset is called again. If @p destroy is @c false the window will not
     * be destroyed. It is then the responsibility of the caller to destroy the window.
     */
    void reset(xcb_connection_t* con, xcb_window_t win, bool destroy = true);
    void reset();

    /**
     * @returns @c true if a window is managed, @c false otherwise.
     */
    bool is_valid() const;
    inline const QRect& geometry() const
    {
        return m_logicGeometry;
    }
    /**
     * Configures the window with a new geometry.
     * @param geometry The new window geometry to be used
     */
    void set_geometry(const QRect& geometry);
    void set_geometry(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void move(const QPoint& pos);
    void move(uint32_t x, uint32_t y);
    void resize(const QSize& size);
    void resize(uint32_t width, uint32_t height);
    void raise();
    void lower();
    void map();
    void unmap();
    void reparent(xcb_window_t parent, int x = 0, int y = 0);
    void change_property(xcb_atom_t prop,
                         xcb_atom_t type,
                         uint8_t format,
                         uint32_t length,
                         const void* data,
                         uint8_t mode = XCB_PROP_MODE_REPLACE);
    void delete_property(xcb_atom_t prop);
    void set_border_width(uint32_t width);
    void grab_button(uint8_t pointerMode,
                     uint8_t keyboardmode,
                     uint16_t modifiers = XCB_MOD_MASK_ANY,
                     uint8_t button = XCB_BUTTON_INDEX_ANY,
                     uint16_t eventMask = XCB_EVENT_MASK_BUTTON_PRESS,
                     xcb_window_t confineTo = XCB_WINDOW_NONE,
                     xcb_cursor_t cursor = XCB_CURSOR_NONE,
                     bool ownerEvents = false);
    void ungrab_button(uint16_t modifiers = XCB_MOD_MASK_ANY,
                       uint8_t button = XCB_BUTTON_INDEX_ANY);
    /**
     * Clears the window area. Same as xcb_clear_area with x, y, width, height being @c 0.
     */
    void clear();
    void set_background_pixmap(xcb_pixmap_t pixmap);
    void define_cursor(xcb_cursor_t cursor);
    void focus(uint8_t revertTo = XCB_INPUT_FOCUS_POINTER_ROOT,
               xcb_timestamp_t time = XCB_TIME_CURRENT_TIME);
    void select_input(uint32_t events);
    void kill();
    operator xcb_window_t() const;

private:
    xcb_window_t do_create(xcb_connection_t* con,
                           xcb_window_t parent,
                           const QRect& geometry,
                           uint16_t windowClass,
                           uint32_t mask = 0,
                           const uint32_t* values = nullptr);
    void destroy();

    xcb_window_t m_window{XCB_WINDOW_NONE};
    bool m_destroy{true};
    QRect m_logicGeometry;
    xcb_connection_t* con{nullptr};
};

inline window::window(xcb_connection_t* con, xcb_window_t win, bool destroy)
    : m_window(win)
    , m_destroy(destroy)
    , con{con}
{
}

inline window::window(xcb_connection_t* con,
                      xcb_window_t parent,
                      QRect const& geometry,
                      uint32_t mask,
                      const uint32_t* values)
    : m_window(do_create(con, parent, geometry, XCB_COPY_FROM_PARENT, mask, values))
    , m_destroy(true)
    , con{con}
{
}

inline window::window(xcb_connection_t* con,
                      xcb_window_t parent,
                      QRect const& geometry,
                      uint16_t windowClass,
                      uint32_t mask,
                      const uint32_t* values)
    : m_window(do_create(con, parent, geometry, windowClass, mask, values))
    , m_destroy(true)
    , con{con}
{
}

inline window::~window()
{
    destroy();
}

inline void window::destroy()
{
    if (!is_valid() || !m_destroy) {
        return;
    }
    xcb_destroy_window(con, m_window);
    m_window = XCB_WINDOW_NONE;
}

inline bool window::is_valid() const
{
    return m_window != XCB_WINDOW_NONE;
}

inline window::operator xcb_window_t() const
{
    return m_window;
}

inline void window::create(xcb_connection_t* con,
                           xcb_window_t parent,
                           QRect const& geometry,
                           uint16_t windowClass,
                           uint32_t mask,
                           const uint32_t* values)
{
    assert(con);
    destroy();
    m_window = do_create(con, parent, geometry, windowClass, mask, values);
}

inline void window::create(xcb_connection_t* con,
                           xcb_window_t parent,
                           QRect const& geometry,
                           uint32_t mask,
                           const uint32_t* values)
{
    create(con, parent, geometry, XCB_COPY_FROM_PARENT, mask, values);
}

inline xcb_window_t window::do_create(xcb_connection_t* con,
                                      xcb_window_t parent,
                                      QRect const& geometry,
                                      uint16_t windowClass,
                                      uint32_t mask,
                                      const uint32_t* values)
{
    this->con = con;
    m_logicGeometry = geometry;
    xcb_window_t w = xcb_generate_id(con);
    xcb_create_window(con,
                      XCB_COPY_FROM_PARENT,
                      w,
                      parent,
                      geometry.x(),
                      geometry.y(),
                      geometry.width(),
                      geometry.height(),
                      0,
                      windowClass,
                      XCB_COPY_FROM_PARENT,
                      mask,
                      values);
    return w;
}

inline void window::reset(xcb_connection_t* con, xcb_window_t window, bool shouldDestroy)
{
    destroy();
    this->con = con;
    m_window = window;
    m_destroy = shouldDestroy;
}

inline void window::reset()
{
    destroy();
    m_window = XCB_WINDOW_NONE;
    m_destroy = true;
}

inline void window::set_geometry(const QRect& geometry)
{
    set_geometry(geometry.x(), geometry.y(), geometry.width(), geometry.height());
}

inline void window::set_geometry(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    m_logicGeometry.setRect(x, y, width, height);
    if (!is_valid()) {
        return;
    }
    const uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH
        | XCB_CONFIG_WINDOW_HEIGHT;
    const uint32_t values[] = {x, y, width, height};
    xcb_configure_window(con, m_window, mask, values);
}

inline void window::move(const QPoint& pos)
{
    move(pos.x(), pos.y());
}

inline void window::move(uint32_t x, uint32_t y)
{
    m_logicGeometry.moveTo(x, y);
    if (!is_valid()) {
        return;
    }
    move_window(con, m_window, x, y);
}

inline void window::resize(const QSize& size)
{
    resize(size.width(), size.height());
}

inline void window::resize(uint32_t width, uint32_t height)
{
    m_logicGeometry.setSize(QSize(width, height));
    if (!is_valid()) {
        return;
    }
    const uint16_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    const uint32_t values[] = {width, height};
    xcb_configure_window(con, m_window, mask, values);
}

inline void window::raise()
{
    const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(con, m_window, XCB_CONFIG_WINDOW_STACK_MODE, values);
}

inline void window::lower()
{
    lower_window(con, m_window);
}

inline void window::map()
{
    if (!is_valid()) {
        return;
    }
    xcb_map_window(con, m_window);
}

inline void window::unmap()
{
    if (!is_valid()) {
        return;
    }
    xcb_unmap_window(con, m_window);
}

inline void window::reparent(xcb_window_t parent, int x, int y)
{
    if (!is_valid()) {
        return;
    }
    xcb_reparent_window(con, m_window, parent, x, y);
}

inline void window::change_property(xcb_atom_t prop,
                                    xcb_atom_t type,
                                    uint8_t format,
                                    uint32_t length,
                                    const void* data,
                                    uint8_t mode)
{
    if (!is_valid()) {
        return;
    }
    xcb_change_property(con, mode, m_window, prop, type, format, length, data);
}

inline void window::delete_property(xcb_atom_t prop)
{
    if (!is_valid()) {
        return;
    }
    xcb_delete_property(con, m_window, prop);
}

inline void window::set_border_width(uint32_t width)
{
    if (!is_valid()) {
        return;
    }
    xcb_configure_window(con, m_window, XCB_CONFIG_WINDOW_BORDER_WIDTH, &width);
}

inline void window::grab_button(uint8_t pointerMode,
                                uint8_t keyboardmode,
                                uint16_t modifiers,
                                uint8_t button,
                                uint16_t eventMask,
                                xcb_window_t confineTo,
                                xcb_cursor_t cursor,
                                bool ownerEvents)
{
    if (!is_valid()) {
        return;
    }
    xcb_grab_button(con,
                    ownerEvents,
                    m_window,
                    eventMask,
                    pointerMode,
                    keyboardmode,
                    confineTo,
                    cursor,
                    button,
                    modifiers);
}

inline void window::ungrab_button(uint16_t modifiers, uint8_t button)
{
    if (!is_valid()) {
        return;
    }
    xcb_ungrab_button(con, button, m_window, modifiers);
}

inline void window::clear()
{
    if (!is_valid()) {
        return;
    }
    xcb_clear_area(con, false, m_window, 0, 0, 0, 0);
}

inline void window::set_background_pixmap(xcb_pixmap_t pixmap)
{
    if (!is_valid()) {
        return;
    }
    const uint32_t values[] = {pixmap};
    xcb_change_window_attributes(con, m_window, XCB_CW_BACK_PIXMAP, values);
}

inline void window::define_cursor(xcb_cursor_t cursor)
{
    xcb::define_cursor(con, m_window, cursor);
}

inline void window::focus(uint8_t revertTo, xcb_timestamp_t time)
{
    xcb_set_input_focus(con, revertTo, m_window, time);
}

inline void window::select_input(uint32_t events)
{
    xcb::select_input(con, m_window, events);
}

inline void window::kill()
{
    xcb_kill_client(con, m_window);
}

}
