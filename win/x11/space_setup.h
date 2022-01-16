/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "color_mapper.h"
#include "space_event.h"
#include "sync_alarm_filter.h"

#include "atoms.h"
#include "main.h"
#include "moving_client_x11_filter.h"
#include "utils.h"
#include "was_user_interaction_x11_filter.h"

#include <KStartupInfo>

namespace KWin::win::x11
{

static void select_wm_input_event_mask()
{
    uint32_t presentMask = 0;
    Xcb::WindowAttributes attr(rootWindow());
    if (!attr.isNull()) {
        presentMask = attr->your_event_mask;
    }

    Xcb::selectInput(rootWindow(),
                     presentMask | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_PROPERTY_CHANGE
                         | XCB_EVENT_MASK_COLOR_MAP_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
                         | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_FOCUS_CHANGE
                         | // For NotifyDetailNone
                         XCB_EVENT_MASK_EXPOSURE);
}

template<typename Space>
void init_space(Space& space)
{
    assert(kwinApp()->x11Connection());

    atoms->retrieveHelpers();

    // first initialize the extensions
    Xcb::Extensions::self();
    auto colormaps = new color_mapper(&space);
    QObject::connect(&space, &Space::clientActivated, colormaps, &color_mapper::update);

    // Call this before XSelectInput() on the root window
    space.startup = new KStartupInfo(
        KStartupInfo::DisableKWinModule | KStartupInfo::AnnounceSilenceChanges, &space);

    // Select windowmanager privileges
    select_wm_input_event_mask();

    // Compatibility
    int32_t data = 1;

    xcb_change_property(connection(),
                        XCB_PROP_MODE_APPEND,
                        rootWindow(),
                        atoms->kwin_running,
                        atoms->kwin_running,
                        32,
                        1,
                        &data);

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        space.m_wasUserInteractionFilter.reset(new WasUserInteractionX11Filter);
        space.m_movingClientFilter.reset(new MovingClientX11Filter);
    }
    if (Xcb::Extensions::self()->isSyncAvailable()) {
        space.m_syncAlarmFilter.reset(new sync_alarm_filter);
    }

    // Needed for proper initialization of user_time in Client ctor
    kwinApp()->update_x11_time_from_clock();

    const uint32_t nullFocusValues[] = {true};
    space.m_nullFocus.reset(new Xcb::Window(QRect(-1, -1, 1, 1),
                                            XCB_WINDOW_CLASS_INPUT_ONLY,
                                            XCB_CW_OVERRIDE_REDIRECT,
                                            nullFocusValues));
    space.m_nullFocus->map();

    auto rootInfo = win::x11::root_info::create();
    auto const vds = VirtualDesktopManager::self();
    vds->setRootInfo(rootInfo);
    rootInfo->activate();

    // TODO: only in X11 mode
    // Extra NETRootInfo instance in Client mode is needed to get the values of the properties
    NETRootInfo client_info(connection(), NET::ActiveWindow | NET::CurrentDesktop);
    if (!qApp->isSessionRestored()) {
        space.m_initialDesktop = client_info.currentDesktop();
        vds->setCurrent(space.m_initialDesktop);
    }

    // TODO: better value
    rootInfo->setActiveWindow(XCB_WINDOW_NONE);
    space.focusToNull();

    if (!qApp->isSessionRestored())
        ++space.block_focus; // Because it will be set below

    {
        // Begin updates blocker block
        Blocker blocker(space.stacking_order);

        Xcb::Tree tree(rootWindow());
        xcb_window_t* wins = xcb_query_tree_children(tree.data());

        QVector<Xcb::WindowAttributes> windowAttributes(tree->children_len);
        QVector<Xcb::WindowGeometry> windowGeometries(tree->children_len);

        // Request the attributes and geometries of all toplevel windows
        for (int i = 0; i < tree->children_len; i++) {
            windowAttributes[i] = Xcb::WindowAttributes(wins[i]);
            windowGeometries[i] = Xcb::WindowGeometry(wins[i]);
        }

        // Get the replies
        for (int i = 0; i < tree->children_len; i++) {
            Xcb::WindowAttributes attr(windowAttributes.at(i));

            if (attr.isNull()) {
                continue;
            }

            if (attr->override_redirect) {
                if (attr->map_state == XCB_MAP_STATE_VIEWABLE
                    && attr->_class != XCB_WINDOW_CLASS_INPUT_ONLY)
                    // ### This will request the attributes again
                    win::x11::create_unmanaged_window(space, wins[i]);
            } else if (attr->map_state != XCB_MAP_STATE_UNMAPPED) {
                if (Application::wasCrash()) {
                    space.fixPositionAfterCrash(wins[i], windowGeometries.at(i).data());
                }

                // ### This will request the attributes again
                win::x11::create_controlled_window(space, wins[i], true);
            }
        }

        // Propagate clients, will really happen at the end of the updates blocker block
        space.stacking_order->update(true);

        space.saveOldScreenSizes();
        space.updateClientArea();

        // NETWM spec says we have to set it to (0,0) if we don't support it
        NETPoint* viewports = new NETPoint[VirtualDesktopManager::self()->count()];
        rootInfo->setDesktopViewport(VirtualDesktopManager::self()->count(), *viewports);
        delete[] viewports;
        QRect geom;
        auto const& screens = kwinApp()->get_base().screens;
        for (int i = 0; i < screens.count(); i++) {
            geom |= screens.geometry(i);
        }
        NETSize desktop_geometry;
        desktop_geometry.width = geom.width();
        desktop_geometry.height = geom.height();
        rootInfo->setDesktopGeometry(desktop_geometry);
        space.setShowingDesktop(false);

    } // End updates blocker block

    // TODO: only on X11?
    Toplevel* new_active_client = nullptr;
    if (!qApp->isSessionRestored()) {
        --space.block_focus;
        new_active_client
            = space.findClient(win::x11::predicate_match::window, client_info.activeWindow());
    }
    if (new_active_client == nullptr && space.activeClient() == nullptr
        && space.should_get_focus.size() == 0) {
        // No client activated in manage()
        if (new_active_client == nullptr)
            new_active_client
                = win::top_client_on_desktop(&space, VirtualDesktopManager::self()->current(), -1);
        if (new_active_client == nullptr) {
            new_active_client
                = win::find_desktop(&space, true, VirtualDesktopManager::self()->current());
        }
    }
    if (new_active_client != nullptr)
        space.activateClient(new_active_client);
}

template<typename Space>
void clear_space(Space& space)
{
    space.stacking_order->lock();

    // Use stacking_order, so that kwin --replace keeps stacking order
    auto const stack = space.stacking_order->sorted();

    // "mutex" the stackingorder, since anything trying to access it from now on will find
    // many dangeling pointers and crash
    space.stacking_order->win_stack.clear();

    // Only release windows on X11.
    auto is_x11 = kwinApp()->operationMode() == Application::OperationModeX11;

    for (auto it = stack.cbegin(), end = stack.cend(); it != end; ++it) {
        auto window = qobject_cast<x11::window*>(const_cast<Toplevel*>(*it));
        if (!window) {
            continue;
        }

        release_window(window, is_x11);

        // No removeClient() is called, it does more than just removing.
        // However, remove from some lists to e.g. prevent performTransiencyCheck()
        // from crashing.
        remove_all(space.m_allClients, window);
        remove_all(space.m_windows, window);
    }

    for (auto const& unmanaged : space.unmanagedList()) {
        release_window(static_cast<window*>(unmanaged), is_x11);
        remove_all(space.m_windows, unmanaged);
        remove_all(space.stacking_order->pre_stack, unmanaged);
    }

    window::cleanupX11();

    space.stacking_order->unlock();
}

}
