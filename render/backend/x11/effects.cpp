/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "base/platform.h"
#include "base/x11/grabs.h"
#include "input/cursor.h"
#include "mouse_intercept_filter.h"
#include "screens.h"
#include "win/screen_edges.h"
#include "win/x11/space.h"

#include <QDesktopWidget>

namespace KWin::render::backend::x11
{

effects_handler_impl::effects_handler_impl(render::compositor* compositor, render::scene* scene)
    : render::effects_handler_impl(compositor, scene)
{
    connect(this, &effects_handler_impl::screenGeometryChanged, this, [this](const QSize& size) {
        if (mouse_intercept.window.is_valid()) {
            mouse_intercept.window.set_geometry(QRect(0, 0, size.width(), size.height()));
        }
    });
}

effects_handler_impl::~effects_handler_impl()
{
    // EffectsHandlerImpl tries to unload all effects when it's destroyed.
    // The routine that unloads effects makes some calls (indirectly) to
    // doUngrabKeyboard and doStopMouseInterception, which are virtual.
    // Given that any call to a virtual function in the destructor of a base
    // class will never go to a derived class, we have to unload effects
    // here. Yeah, this is quite a bit ugly but it's fine; someday, X11
    // will be dead (or not?).
    unloadAllEffects();
}

bool effects_handler_impl::doGrabKeyboard()
{
    bool ret = base::x11::grab_keyboard();
    if (!ret)
        return false;
    // Workaround for Qt 5.9 regression introduced with 2b34aefcf02f09253473b096eb4faffd3e62b5f4
    // we no longer get any events for the root window, one needs to call winId() on the desktop
    // window
    // TODO: change effects event handling to create the appropriate QKeyEvent without relying on Qt
    // as it's done already in the Wayland case.
    qApp->desktop()->winId();
    return ret;
}

void effects_handler_impl::doUngrabKeyboard()
{
    base::x11::ungrab_keyboard();
}

void effects_handler_impl::doStartMouseInterception(Qt::CursorShape shape)
{
    // NOTE: it is intended to not perform an XPointerGrab on X11. See documentation in
    // kwineffects.h The mouse grab is implemented by using a full screen input only window
    if (!mouse_intercept.window.is_valid()) {
        auto const& s = kwinApp()->get_base().screens.size();
        const QRect geo(0, 0, s.width(), s.height());
        const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
        const uint32_t values[] = {true,
                                   XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                       | XCB_EVENT_MASK_POINTER_MOTION};
        mouse_intercept.window.reset(base::x11::xcb::create_input_window(geo, mask, values));
        defineCursor(shape);
    } else {
        defineCursor(shape);
    }

    mouse_intercept.window.map();
    mouse_intercept.window.raise();
    mouse_intercept.filter = std::make_unique<mouse_intercept_filter>(mouse_intercept.window, this);

    // Raise electric border windows above the input windows
    // so they can still be triggered.
    workspace()->edges->ensureOnTop();
}

void effects_handler_impl::doStopMouseInterception()
{
    mouse_intercept.window.unmap();
    mouse_intercept.filter.reset();
    win::x11::stack_screen_edges_under_override_redirect(workspace());
}

void effects_handler_impl::defineCursor(Qt::CursorShape shape)
{
    auto const c = input::get_cursor()->x11_cursor(shape);
    if (c != XCB_CURSOR_NONE) {
        mouse_intercept.window.define_cursor(c);
    }
}

void effects_handler_impl::doCheckInputWindowStacking()
{
    mouse_intercept.window.raise();

    // Raise electric border windows above the input windows
    // so they can still be triggered. TODO: Do both at once.
    workspace()->edges->ensureOnTop();
}

}
