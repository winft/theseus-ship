/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "net.h"

#include <kwin_export.h>

#include <QSize>
#include <xcb/xcb.h>

namespace KWin::win::x11::net
{

struct root_info_private;

class KWIN_EXPORT root_info
{
public:
    /**
        Indexes for the properties array.
    **/
    // update also root_info_private::properties[] size when extending this
    enum {
        PROTOCOLS,
        WINDOW_TYPES,
        STATES,
        PROTOCOLS2,
        ACTIONS,
        PROPERTIES_SIZE,
    };

    root_info(xcb_connection_t* connection,
              xcb_window_t supportWindow,
              const char* wmName,
              net::Properties properties,
              window_type_mask windowTypes,
              net::States states,
              net::Properties2 properties2,
              net::Actions actions,
              int screen = -1,
              bool doActivate = true);
    root_info(xcb_connection_t* connection,
              net::Properties properties,
              net::Properties2 properties2 = net::Properties2(),
              int screen = -1,
              bool doActivate = true);
    root_info(const root_info& rootinfo);
    virtual ~root_info();

    xcb_connection_t* xcbConnection() const;
    xcb_window_t rootWindow() const;
    xcb_window_t supportWindow() const;
    const char* wmName() const;

    void setSupported(net::Property property, bool on = true);
    void setSupported(net::Property2 property, bool on = true);
    void setSupported(window_type_mask property, bool on = true);
    void setSupported(net::State property, bool on = true);
    void setSupported(net::Action property, bool on = true);

    bool isSupported(net::Property property) const;
    bool isSupported(net::Property2 property) const;
    bool isSupported(window_type_mask type) const;
    bool isSupported(net::State state) const;
    bool isSupported(net::Action action) const;

    net::Properties supportedProperties() const;
    net::Properties2 supportedProperties2() const;
    net::States supportedStates() const;
    window_type_mask supportedWindowTypes() const;
    net::Actions supportedActions() const;

    net::Properties passedProperties() const;
    net::Properties2 passedProperties2() const;
    net::States passedStates() const;
    window_type_mask passedWindowTypes() const;
    net::Actions passedActions() const;

    const xcb_window_t* clientList() const;
    int clientListCount() const;
    const xcb_window_t* clientListStacking() const;
    int clientListStackingCount() const;

    net::size desktopGeometry() const;
    net::point desktopViewport(int desktop) const;
    net::rect workArea(int desktop) const;

    const char* desktopName(int desktop) const;

    const xcb_window_t* virtualRoots() const;
    int virtualRootsCount() const;

    net::Orientation desktopLayoutOrientation() const;
    QSize desktopLayoutColumnsRows() const;
    net::DesktopLayoutCorner desktopLayoutCorner() const;

    xcb_window_t activeWindow() const;
    void activate();

    void setClientList(const xcb_window_t* windows, unsigned int count);
    void setClientListStacking(const xcb_window_t* windows, unsigned int count);

    void setCurrentDesktop(int desktop, bool ignore_viewport = false);
    void setDesktopGeometry(const net::size& geometry);
    void setDesktopViewport(int desktop, const net::point& viewport);
    void setNumberOfDesktops(int numberOfDesktops);
    void setDesktopName(int desktop, const char* desktopName);

    void setActiveWindow(xcb_window_t window,
                         net::RequestSource src,
                         xcb_timestamp_t timestamp,
                         xcb_window_t active_window);
    void setActiveWindow(xcb_window_t window);

    void setWorkArea(int desktop, const net::rect& workArea);
    void setVirtualRoots(const xcb_window_t* windows, unsigned int count);
    void setDesktopLayout(net::Orientation orientation,
                          int columns,
                          int rows,
                          net::DesktopLayoutCorner corner);
    void setShowingDesktop(bool showing);
    bool showingDesktop() const;

    const root_info& operator=(const root_info& rootinfo);

    void closeWindowRequest(xcb_window_t window);
    void moveResizeRequest(xcb_window_t window, int x_root, int y_root, Direction direction);
    void
    moveResizeWindowRequest(xcb_window_t window, int flags, int x, int y, int width, int height);
    void showWindowMenuRequest(xcb_window_t window, int device_id, int x_root, int y_root);
    void restackRequest(xcb_window_t window,
                        RequestSource source,
                        xcb_window_t above,
                        int detail,
                        xcb_timestamp_t timestamp);
    void sendPing(xcb_window_t window, xcb_timestamp_t timestamp);

    void event(xcb_generic_event_t* event,
               net::Properties* properties,
               net::Properties2* properties2 = nullptr);
    net::Properties event(xcb_generic_event_t* event);

protected:
    virtual void addClient(xcb_window_t window)
    {
        Q_UNUSED(window);
    }

    virtual void removeClient(xcb_window_t window)
    {
        Q_UNUSED(window);
    }

    virtual void changeNumberOfDesktops(int numberOfDesktops)
    {
        Q_UNUSED(numberOfDesktops);
    }

    virtual void changeDesktopGeometry(int desktop, const net::size& geom)
    {
        Q_UNUSED(desktop);
        Q_UNUSED(geom);
    }

    virtual void changeDesktopViewport(int desktop, const net::point& viewport)
    {
        Q_UNUSED(desktop);
        Q_UNUSED(viewport);
    }

    virtual void changeCurrentDesktop(int desktop)
    {
        Q_UNUSED(desktop);
    }

    virtual void closeWindow(xcb_window_t window)
    {
        Q_UNUSED(window);
    }

    virtual void moveResize(xcb_window_t window, int x_root, int y_root, unsigned long direction)
    {
        Q_UNUSED(window);
        Q_UNUSED(x_root);
        Q_UNUSED(y_root);
        Q_UNUSED(direction);
    }

    virtual void gotPing(xcb_window_t window, xcb_timestamp_t timestamp)
    {
        Q_UNUSED(window);
        Q_UNUSED(timestamp);
    }

    virtual void changeActiveWindow(xcb_window_t window,
                                    net::RequestSource src,
                                    xcb_timestamp_t timestamp,
                                    xcb_window_t active_window)
    {
        Q_UNUSED(window);
        Q_UNUSED(src);
        Q_UNUSED(timestamp);
        Q_UNUSED(active_window);
    }

    virtual void
    moveResizeWindow(xcb_window_t window, int flags, int x, int y, int width, int height)
    {
        Q_UNUSED(window);
        Q_UNUSED(flags);
        Q_UNUSED(x);
        Q_UNUSED(y);
        Q_UNUSED(width);
        Q_UNUSED(height);
    }

    virtual void restackWindow(xcb_window_t /*window*/,
                               RequestSource /*source*/,
                               xcb_window_t /*above*/,
                               int /*detail*/,
                               xcb_timestamp_t /*timestamp*/)
    {
    }

    virtual void changeShowingDesktop(bool /*showing*/)
    {
    }

    virtual void
    showWindowMenu(xcb_window_t /*window*/, int /*device_id*/, int /*x_root*/, int /*y_root*/)
    {
    }

private:
    void update(net::Properties properties, net::Properties2 properties2);
    void setSupported();
    void setDefaultProperties();
    void updateSupportedProperties(xcb_atom_t atom);

protected:
    virtual void virtual_hook(int id, void* data);

private:
    root_info_private* p;
};

}
