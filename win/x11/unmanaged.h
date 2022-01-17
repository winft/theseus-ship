/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "window_release.h"

#include "win/remnant.h"
#include "win/space.h"

#include "render/effects.h"
#include "utils.h"
#include "xcbutils.h"

namespace KWin::win::x11
{

template<typename Win>
Win* create_unmanaged_window(xcb_window_t w)
{
    // Window types that are supported as unmanaged (mainly for compositing).
    NET::WindowTypes constexpr supported_default_types = NET::NormalMask | NET::DesktopMask
        | NET::DockMask | NET::ToolbarMask | NET::MenuMask
        | NET::DialogMask /*| NET::OverrideMask*/ | NET::TopMenuMask | NET::UtilityMask
        | NET::SplashMask | NET::DropdownMenuMask | NET::PopupMenuMask | NET::TooltipMask
        | NET::NotificationMask | NET::ComboBoxMask | NET::DNDIconMask | NET::OnScreenDisplayMask
        | NET::CriticalNotificationMask;

    XServerGrabber xserverGrabber;
    Xcb::WindowAttributes attr(w);
    Xcb::WindowGeometry geo(w);

    if (attr.isNull() || attr->map_state != XCB_MAP_STATE_VIEWABLE) {
        return nullptr;
    }
    if (attr->_class == XCB_WINDOW_CLASS_INPUT_ONLY) {
        return nullptr;
    }
    if (geo.isNull()) {
        return nullptr;
    }

    auto win = new Win;

    win->supported_default_types = supported_default_types;
    win->set_layer(win::layer::unmanaged);

    QTimer::singleShot(50, win, &Win::setReadyForPainting);

    // The window is also the frame.
    win->setWindowHandles(w);
    Xcb::selectInput(w,
                     attr->your_event_mask | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                         | XCB_EVENT_MASK_PROPERTY_CHANGE);
    win->set_frame_geometry(geo.rect());
    win->checkScreen();
    win->m_visual = attr->visual;
    win->bit_depth = geo->depth;
    win->info = new NETWinInfo(connection(),
                               w,
                               rootWindow(),
                               NET::WMWindowType | NET::WMPid,
                               NET::WM2Opacity | NET::WM2WindowRole | NET::WM2WindowClass
                                   | NET::WM2OpaqueRegion);
    win->getResourceClass();
    win->getWmClientLeader();
    win->getWmClientMachine();
    if (Xcb::Extensions::self()->isShapeAvailable()) {
        xcb_shape_select_input(connection(), w, true);
    }
    win->detectShape(w);
    win->getWmOpaqueRegion();
    win->getSkipCloseAnimation();
    win->setupCompositing(true);

    auto find_internal_window = [&win]() -> QWindow* {
        auto const windows = kwinApp()->topLevelWindows();
        for (auto w : windows) {
            if (w->winId() == win->xcb_window()) {
                return w;
            }
        }
        return nullptr;
    };

    if (auto internalWindow = find_internal_window()) {
        win->is_outline = internalWindow->property("__kwin_outline").toBool();
    }
    if (effects) {
        static_cast<render::effects_handler_impl*>(effects)->checkInputWindowStacking();
    }
    return win;
}

template<typename Win>
void unmanaged_configure_event(Win* win, xcb_configure_notify_event_t* e)
{
    if (effects) {
        // keep them on top
        static_cast<render::effects_handler_impl*>(effects)->checkInputWindowStacking();
    }
    QRect newgeom(e->x, e->y, e->width, e->height);
    if (newgeom != win->frameGeometry()) {
        // Damage old area.
        win->addWorkspaceRepaint(win::visible_rect(win));

        auto const old = win->frameGeometry();
        win->set_frame_geometry(newgeom);

        win->addRepaintFull();

        if (old.size() != win->frameGeometry().size()) {
            win->discardWindowPixmap();
        }
        Q_EMIT win->frame_geometry_changed(win, old);
    }
}

template<typename Win>
bool unmanaged_event(Win* win, xcb_generic_event_t* e)
{
    auto old_opacity = win->opacity();
    NET::Properties dirtyProperties;
    NET::Properties2 dirtyProperties2;

    // Pass through the NET stuff.
    win->info->event(e, &dirtyProperties, &dirtyProperties2);

    if (dirtyProperties2 & NET::WM2Opacity) {
        if (win::compositing()) {
            win->addRepaintFull();
            Q_EMIT win->opacityChanged(win, old_opacity);
        }
    }
    if (dirtyProperties2 & NET::WM2OpaqueRegion) {
        win->getWmOpaqueRegion();
    }
    if (dirtyProperties2.testFlag(NET::WM2WindowRole)) {
        Q_EMIT win->windowRoleChanged();
    }
    if (dirtyProperties2.testFlag(NET::WM2WindowClass)) {
        win->getResourceClass();
    }

    auto const eventType = e->response_type & ~0x80;
    switch (eventType) {
    case XCB_DESTROY_NOTIFY:
        destroy_window(win);
        break;
    case XCB_UNMAP_NOTIFY: {
        // may cause leave event
        workspace()->updateFocusMousePosition(input::get_cursor()->pos());

        // unmap notify might have been emitted due to a destroy notify
        // but unmap notify gets emitted before the destroy notify, nevertheless at this
        // point the window is already destroyed. This means any XCB request with the window
        // will cause an error.
        // To not run into these errors we try to wait for the destroy notify. For this we
        // generate a round trip to the X server and wait a very short time span before
        // handling the release.
        kwinApp()->update_x11_time_from_clock();
        // using 1 msec to not just move it at the end of the event loop but add an very short
        // timespan to cover cases like unmap() followed by destroy(). The only other way to
        // ensure that the window is not destroyed when we do the release handling is to grab
        // the XServer which we do not want to do for an Unmanaged. The timespan of 1 msec is
        // short enough to not cause problems in the close window animations.
        // It's of course still possible that we miss the destroy in which case non-fatal
        // X errors are reported to the event loop and logged by Qt.
        win->has_scheduled_release = true;
        QTimer::singleShot(1, win, [win] { release_unmanaged(win); });
        break;
    }
    case XCB_CONFIGURE_NOTIFY:
        unmanaged_configure_event(win, reinterpret_cast<xcb_configure_notify_event_t*>(e));
        break;
    case XCB_PROPERTY_NOTIFY:
        property_notify_event_prepare(*win, reinterpret_cast<xcb_property_notify_event_t*>(e));
        break;
    case XCB_CLIENT_MESSAGE:
        win->clientMessageEvent(reinterpret_cast<xcb_client_message_event_t*>(e));
        break;
    default: {
        if (eventType == Xcb::Extensions::self()->shapeNotifyEvent()) {
            win->detectShape(win->xcb_window());
            win->addRepaintFull();

            // In case shape change removes part of this window.
            win->addWorkspaceRepaint(win->frameGeometry());
            Q_EMIT win->frame_geometry_changed(win, win->frameGeometry());
        }
        if (eventType == Xcb::Extensions::self()->damageNotifyEvent()) {
            win->damageNotifyEvent();
        }
        break;
    }
    }
    // Don't eat events, even our own unmanaged widgets are tracked.
    return false;
}

}
