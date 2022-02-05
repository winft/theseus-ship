/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "base/x11/xcb/extensions.h"
#include "render/compositor.h"

#include <xcb/glx.h>

#ifndef XCB_GLX_BUFFER_SWAP_COMPLETE
#define XCB_GLX_BUFFER_SWAP_COMPLETE 1
typedef struct xcb_glx_buffer_swap_complete_event_t {
    uint8_t response_type;       /**<  */
    uint8_t pad0;                /**<  */
    uint16_t sequence;           /**<  */
    uint16_t event_type;         /**<  */
    uint8_t pad1[2];             /**<  */
    xcb_glx_drawable_t drawable; /**<  */
    uint32_t ust_hi;             /**<  */
    uint32_t ust_lo;             /**<  */
    uint32_t msc_hi;             /**<  */
    uint32_t msc_lo;             /**<  */
    uint32_t sbc;                /**<  */
} xcb_glx_buffer_swap_complete_event_t;
#endif

namespace KWin::render::backend::x11
{

class swap_event_filter : public base::x11::event_filter
{
public:
    swap_event_filter(xcb_drawable_t drawable, xcb_glx_drawable_t glxDrawable)
        : base::x11::event_filter(base::x11::xcb::extensions::self()->glx_event_base()
                                  + XCB_GLX_BUFFER_SWAP_COMPLETE)
        , m_drawable(drawable)
        , m_glxDrawable(glxDrawable)
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        xcb_glx_buffer_swap_complete_event_t* ev
            = reinterpret_cast<xcb_glx_buffer_swap_complete_event_t*>(event);

        // The drawable field is the X drawable when the event was synthesized
        // by a WireToEvent handler, and the GLX drawable when the event was
        // received over the wire
        if (ev->drawable == m_drawable || ev->drawable == m_glxDrawable) {
            render::compositor::self()->bufferSwapComplete();
            return true;
        }

        return false;
    }

private:
    xcb_drawable_t m_drawable;
    xcb_glx_drawable_t m_glxDrawable;
};

}
