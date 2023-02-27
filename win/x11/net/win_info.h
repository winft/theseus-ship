/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "net.h"

#include <kwin_export.h>

#include <vector>
#include <xcb/xcb.h>

namespace KWin::win::x11::net
{

struct win_info_private;
template<class Z>
class rarray;

class KWIN_EXPORT win_info
{
public:
    // update also win_info_private::properties[] size when extending this
    enum {
        PROTOCOLS,
        PROTOCOLS2,
        PROPERTIES_SIZE,
    };

    win_info(xcb_connection_t* connection,
             xcb_window_t window,
             xcb_window_t rootWindow,
             net::Properties properties,
             net::Properties2 properties2,
             Role role = Client);
    win_info(const win_info& wininfo);
    virtual ~win_info();

    const win_info& operator=(const win_info& wintinfo);

    xcb_connection_t* xcbConnection() const;
    bool hasNETSupport() const;

    net::Properties passedProperties() const;
    net::Properties2 passedProperties2() const;

    net::rect iconGeometry() const;
    net::States state() const;

    net::extended_strut extendedStrut() const;
    net::strut strut() const;

    win::window_type windowType(win::window_type_mask supported_types) const;
    bool hasWindowType() const;

    const char* name() const;
    const char* visibleName() const;
    const char* iconName() const;
    const char* visibleIconName() const;
    int desktop(/*bool ignore_viewport = false*/) const;
    int pid() const;

    bool handledIcons() const;
    MappingState mappingState() const;
    void setIcon(net::icon icon, bool replace = true);
    void setIconGeometry(net::rect geometry);

    void setExtendedStrut(net::extended_strut const& extended_strut);
    void setStrut(net::strut strut);
    void setState(net::States state, net::States mask);
    void setWindowType(win::window_type type);

    void setName(const char* name);
    void setVisibleName(const char* visibleName);
    void setIconName(const char* name);
    void setVisibleIconName(const char* name);

    void setDesktop(int desktop, bool ignore_viewport = false);
    void setPid(int pid);
    void setHandledIcons(bool handled);

    void setFrameExtents(net::strut strut);
    net::strut frameExtents() const;

    void setFrameOverlap(net::strut strut);
    net::strut frameOverlap() const;

    void setGtkFrameExtents(net::strut strut);
    net::strut gtkFrameExtents() const;

    net::icon icon(int width = -1, int height = -1) const;
    const int* iconSizes() const;

    void setUserTime(xcb_timestamp_t time);
    xcb_timestamp_t userTime() const;

    void setStartupId(const char* startup_id);
    const char* startupId() const;

    void setOpacity(unsigned long opacity);
    void setOpacityF(qreal opacity);
    unsigned long opacity() const;
    qreal opacityF() const;

    void setAllowedActions(net::Actions actions);
    net::Actions allowedActions() const;

    xcb_window_t transientFor() const;
    xcb_window_t groupLeader() const;

    bool urgency() const;
    bool input() const;

    MappingState initialMappingState() const;

    xcb_pixmap_t icccmIconPixmap() const;
    xcb_pixmap_t icccmIconPixmapMask() const;

    const char* windowClassClass() const;
    const char* windowClassName() const;
    const char* windowRole() const;
    const char* clientMachine() const;

    void setBlockingCompositing(bool active);
    bool isBlockingCompositing() const;

    void kdeGeometry(net::rect& frame, net::rect& window);

    void setFullscreenMonitors(net::fullscreen_monitors topology);
    net::fullscreen_monitors fullscreenMonitors() const;

    void event(xcb_generic_event_t* event,
               net::Properties* properties,
               net::Properties2* properties2 = nullptr);
    net::Properties event(xcb_generic_event_t* event);

    net::Protocols protocols() const;
    bool supportsProtocol(net::Protocol protocol) const;

    std::vector<net::rect> opaqueRegion() const;

    void setDesktopFileName(const char* name);
    const char* desktopFileName() const;

    const char* gtkApplicationId() const;
    void setAppMenuServiceName(const char* name);
    void setAppMenuObjectPath(const char* path);
    const char* appMenuServiceName() const;
    const char* appMenuObjectPath() const;

    static const int OnAllDesktops;

protected:
    virtual void changeDesktop(int /*desktop*/)
    {
    }

    virtual void changeState(net::States /*state*/, net::States /*mask*/)
    {
    }

    virtual void changeFullscreenMonitors(net::fullscreen_monitors /*topology*/)
    {
    }

private:
    void update(net::Properties dirtyProperties,
                net::Properties2 dirtyProperties2 = net::Properties2());
    void updateWMState();
    void setIconInternal(net::rarray<net::icon>& icons,
                         int& icon_count,
                         xcb_atom_t property,
                         net::icon icon,
                         bool replace);
    net::icon
    iconInternal(net::rarray<net::icon>& icons, int icon_count, int width, int height) const;

protected:
    virtual void virtual_hook(int id, void* data);

private:
    win_info_private* p;
};

}
