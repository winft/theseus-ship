/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "render/x11/overlay_window.h"

namespace KWin::render::x11
{

class KWIN_EXPORT overlay_window_impl : public overlay_window, public base::x11::event_filter
{
public:
    overlay_window_impl();
    ~overlay_window_impl() override;
    /// Creates XComposite overlay window, call initOverlay() afterwards
    bool create() override;
    /// Init overlay and the destination window in it
    void setup(xcb_window_t window) override;
    void show() override;
    void hide() override; // hides and resets overlay window
    void setShape(const QRegion& reg) override;
    void resize(const QSize& size) override;
    /// Destroys XComposite overlay window
    void destroy() override;
    xcb_window_t window() const override;
    bool isVisible() const override;
    void setVisibility(bool visible) override;

    bool event(xcb_generic_event_t* event) override;

private:
    void setNoneBackgroundPixmap(xcb_window_t window);
    void setupInputShape(xcb_window_t window);
    bool m_visible;
    bool m_shown; // For showOverlay()
    QRegion m_shape;
    xcb_window_t m_window;
};

}
