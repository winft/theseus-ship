/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>

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
#include "overlay_window.h"

#include "compositor.h"

#include "base/platform.h"
#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/helpers.h"
#include "base/x11/xcb/proto.h"
#include "kwinglobals.h"
#include "main.h"
#include "win/space.h"

#include <QVector>

#include <xcb/composite.h>
#include <xcb/shape.h>

namespace KWin::render::x11
{
overlay_window::overlay_window(x11::compositor<platform>& compositor)
    : base::x11::event_filter(QVector<int>{XCB_EXPOSE, XCB_VISIBILITY_NOTIFY})
    , m_visible(true)
    , m_shown(false)
    , m_window(XCB_WINDOW_NONE)
    , compositor{compositor}
{
}

overlay_window::~overlay_window()
{
}

bool overlay_window::create()
{
    Q_ASSERT(m_window == XCB_WINDOW_NONE);

    if (!base::x11::xcb::extensions::self()->is_composite_overlay_available()) {
        return false;
    }
    if (!base::x11::xcb::extensions::self()->is_shape_input_available()) {
        // needed in setupOverlay()
        return false;
    }

    base::x11::xcb::overlay_window overlay(rootWindow());
    if (overlay.is_null()) {
        return false;
    }
    m_window = overlay->overlay_win;
    if (m_window == XCB_WINDOW_NONE)
        return false;
    resize(kwinApp()->get_base().topology.size);
    return true;
}

void overlay_window::setup(xcb_window_t window)
{
    Q_ASSERT(m_window != XCB_WINDOW_NONE);
    Q_ASSERT(base::x11::xcb::extensions::self()->is_shape_input_available());

    setNoneBackgroundPixmap(m_window);
    m_shape = QRegion();
    setShape(QRect({}, kwinApp()->get_base().topology.size));
    if (window != XCB_WINDOW_NONE) {
        setNoneBackgroundPixmap(window);
        setupInputShape(window);
    }
    const uint32_t eventMask = XCB_EVENT_MASK_VISIBILITY_CHANGE;
    xcb_change_window_attributes(connection(), m_window, XCB_CW_EVENT_MASK, &eventMask);
}

void overlay_window::setupInputShape(xcb_window_t window)
{
    xcb_shape_rectangles(connection(),
                         XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_INPUT,
                         XCB_CLIP_ORDERING_UNSORTED,
                         window,
                         0,
                         0,
                         0,
                         nullptr);
}

void overlay_window::setNoneBackgroundPixmap(xcb_window_t window)
{
    const uint32_t mask = XCB_BACK_PIXMAP_NONE;
    xcb_change_window_attributes(connection(), window, XCB_CW_BACK_PIXMAP, &mask);
}

void overlay_window::show()
{
    Q_ASSERT(m_window != XCB_WINDOW_NONE);
    if (m_shown)
        return;
    xcb_map_subwindows(connection(), m_window);
    xcb_map_window(connection(), m_window);
    m_shown = true;
}

void overlay_window::hide()
{
    Q_ASSERT(m_window != XCB_WINDOW_NONE);
    xcb_unmap_window(connection(), m_window);
    m_shown = false;
    setShape(QRect({}, kwinApp()->get_base().topology.size));
}

void overlay_window::setShape(const QRegion& reg)
{
    // Avoid setting the same shape again, it causes flicker (apparently it is not a no-op
    // and triggers something).
    if (reg == m_shape)
        return;
    QVector<xcb_rectangle_t> const xrects = base::x11::xcb::qt_region_to_rects(reg);
    xcb_shape_rectangles(connection(),
                         XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_BOUNDING,
                         XCB_CLIP_ORDERING_UNSORTED,
                         m_window,
                         0,
                         0,
                         xrects.count(),
                         xrects.data());
    setupInputShape(m_window);
    m_shape = reg;
}

void overlay_window::resize(const QSize& size)
{
    Q_ASSERT(m_window != XCB_WINDOW_NONE);
    const uint32_t geometry[2]
        = {static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height())};
    xcb_configure_window(
        connection(), m_window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, geometry);
    setShape(QRegion(0, 0, size.width(), size.height()));
}

bool overlay_window::isVisible() const
{
    return m_visible;
}

void overlay_window::setVisibility(bool visible)
{
    m_visible = visible;
}

void overlay_window::destroy()
{
    if (m_window == XCB_WINDOW_NONE)
        return;
    // reset the overlay shape
    auto const& space_size = kwinApp()->get_base().topology.size;
    xcb_rectangle_t rec = {0,
                           0,
                           static_cast<uint16_t>(space_size.width()),
                           static_cast<uint16_t>(space_size.height())};
    xcb_shape_rectangles(connection(),
                         XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_BOUNDING,
                         XCB_CLIP_ORDERING_UNSORTED,
                         m_window,
                         0,
                         0,
                         1,
                         &rec);
    xcb_shape_rectangles(connection(),
                         XCB_SHAPE_SO_SET,
                         XCB_SHAPE_SK_INPUT,
                         XCB_CLIP_ORDERING_UNSORTED,
                         m_window,
                         0,
                         0,
                         1,
                         &rec);
    xcb_composite_release_overlay_window(connection(), m_window);
    m_window = XCB_WINDOW_NONE;
    m_shown = false;
}

xcb_window_t overlay_window::window() const
{
    return m_window;
}

bool overlay_window::event(xcb_generic_event_t* event)
{
    const uint8_t eventType = event->response_type & ~0x80;
    if (eventType == XCB_EXPOSE) {
        const auto* expose = reinterpret_cast<xcb_expose_event_t*>(event);
        if (expose->window == rootWindow() // root window needs repainting
            || (m_window != XCB_WINDOW_NONE
                && expose->window == m_window)) { // overlay needs repainting
            compositor.addRepaint(QRegion(expose->x, expose->y, expose->width, expose->height));
        }
    } else if (eventType == XCB_VISIBILITY_NOTIFY) {
        const auto* visibility = reinterpret_cast<xcb_visibility_notify_event_t*>(event);
        if (m_window != XCB_WINDOW_NONE && visibility->window == m_window) {
            bool was_visible = isVisible();
            setVisibility((visibility->state != XCB_VISIBILITY_FULLY_OBSCURED));
            if (!was_visible && m_visible) {
                // hack for #154825
                compositor.addRepaintFull();
                QTimer::singleShot(2000, compositor.qobject.get(), [comp = &compositor] {
                    comp->addRepaintFull();
                });
            }
            compositor.schedule_repaint();
        }
    }
    return false;
}

}
