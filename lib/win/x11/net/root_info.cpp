/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "root_info.h"

#include "info_p.h"

#include "win/x11/extras.h"

#include <assert.h>
#include <private/qtx11extras_p.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xproto.h>

namespace KWin::win::x11::net
{

struct root_info_private {
    net::Role role;

    // information about the X server
    xcb_connection_t* conn;
    net::size rootSize;
    xcb_window_t root;
    xcb_window_t supportwindow;
    const char* name;

    uint32_t* temp_buf;
    size_t temp_buf_size;

    // data that changes (either by the window manager or by a client)
    // and requires updates
    rarray<net::point> viewport;
    rarray<net::rect> workarea;
    net::size geometry;
    xcb_window_t active;
    xcb_window_t *clients, *stacking, *virtual_roots;
    rarray<const char*> desktop_names;
    int number_of_desktops;
    int current_desktop;

    unsigned long clients_count, stacking_count, virtual_roots_count;
    bool showing_desktop;
    net::Orientation desktop_layout_orientation;
    net::DesktopLayoutCorner desktop_layout_corner;
    int desktop_layout_columns, desktop_layout_rows;

    net::Properties properties;
    net::Properties2 properties2;
    window_type_mask windowTypes;
    net::States states;
    net::Actions actions;
    net::Properties clientProperties;
    net::Properties2 clientProperties2;

    int ref;

    QSharedDataPointer<Atoms> atoms;
    xcb_atom_t atom(KwsAtom atom) const
    {
        return atoms->atom(atom);
    }
};

static uint32_t const netwm_sendevent_mask
    = (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);

const long MAX_PROP_SIZE = 100000;

static char* nstrdup(const char* s1)
{
    if (!s1) {
        return (char*)nullptr;
    }

    int l = strlen(s1) + 1;
    char* s2 = new char[l];
    strncpy(s2, s1, l);
    return s2;
}

static char* nstrndup(const char* s1, int l)
{
    if (!s1 || l == 0) {
        return (char*)nullptr;
    }

    char* s2 = new char[l + 1];
    strncpy(s2, s1, l);
    s2[l] = '\0';
    return s2;
}

static xcb_window_t* nwindup(const xcb_window_t* w1, int n)
{
    if (!w1 || n == 0) {
        return (xcb_window_t*)nullptr;
    }

    xcb_window_t* w2 = new xcb_window_t[n];
    while (n--) {
        w2[n] = w1[n];
    }
    return w2;
}

static void refdec_nri(root_info_private* p)
{
    if (!--p->ref) {
        delete[] p->name;
        delete[] p->stacking;
        delete[] p->clients;
        delete[] p->virtual_roots;
        delete[] p->temp_buf;

        int i;
        for (i = 0; i < p->desktop_names.size(); i++) {
            delete[] p->desktop_names[i];
        }
    }
}

QByteArray
get_string_reply(xcb_connection_t* c, const xcb_get_property_cookie_t cookie, xcb_atom_t type)
{
    xcb_get_property_reply_t* reply = xcb_get_property_reply(c, cookie, nullptr);
    if (!reply) {
        return QByteArray();
    }

    QByteArray value;

    if (reply->type == type && reply->format == 8 && reply->value_len > 0) {
        const char* data = (const char*)xcb_get_property_value(reply);
        int len = reply->value_len;

        if (data) {
            value = QByteArray(data, data[len - 1] ? len : len - 1);
        }
    }

    free(reply);
    return value;
}

QList<QByteArray>
get_stringlist_reply(xcb_connection_t* c, const xcb_get_property_cookie_t cookie, xcb_atom_t type)
{
    xcb_get_property_reply_t* reply = xcb_get_property_reply(c, cookie, nullptr);
    if (!reply) {
        return QList<QByteArray>();
    }

    QList<QByteArray> list;

    if (reply->type == type && reply->format == 8 && reply->value_len > 0) {
        const char* data = (const char*)xcb_get_property_value(reply);
        int len = reply->value_len;

        if (data) {
            const QByteArray ba = QByteArray(data, data[len - 1] ? len : len - 1);
            list = ba.split('\0');
        }
    }

    free(reply);
    return list;
}

void send_client_message(xcb_connection_t* c,
                         uint32_t mask,
                         xcb_window_t destination,
                         xcb_window_t window,
                         xcb_atom_t message,
                         uint32_t const data[])
{
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.sequence = 0;
    event.window = window;
    event.type = message;

    for (int i = 0; i < 5; i++) {
        event.data.data32[i] = data[i];
    }

    xcb_send_event(c, false, destination, mask, (const char*)&event);
}

/*
 The viewport<->desktop matching is a bit backwards, since NET* classes are the base
 (and were originally even created with the intention of being the reference WM spec
 implementation) and KWindowSystem builds on top of it. However it's simpler to add watching
 whether the WM uses viewport is simpler to KWindowSystem and not having this mapping
 in NET* classes could result in some code using it directly and not supporting viewport.
 So NET* classes check if mapping is needed and if yes they forward to KWindowSystem,
 which will forward again back to NET* classes, but to viewport calls instead of desktop calls.
*/

// Construct a new root_info object.

root_info::root_info(xcb_connection_t* connection,
                     xcb_window_t supportWindow,
                     const char* wmName,
                     net::Properties properties,
                     window_type_mask windowTypes,
                     net::States states,
                     net::Properties2 properties2,
                     net::Actions actions,
                     int screen,
                     bool doActivate)
{
    p = new root_info_private;
    p->ref = 1;
    p->atoms = atomsForConnection(connection);

    p->name = nstrdup(wmName);

    p->conn = connection;

    p->temp_buf = nullptr;
    p->temp_buf_size = 0;

    const xcb_setup_t* setup = xcb_get_setup(p->conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);

    if (screen != -1 && screen < setup->roots_len) {
        for (int i = 0; i < screen; i++) {
            xcb_screen_next(&it);
        }
    }

    p->root = it.data->root;
    p->supportwindow = supportWindow;
    p->number_of_desktops = p->current_desktop = 0;
    p->active = XCB_WINDOW_NONE;
    p->clients = p->stacking = p->virtual_roots = (xcb_window_t*)nullptr;
    p->clients_count = p->stacking_count = p->virtual_roots_count = 0;
    p->showing_desktop = false;
    p->desktop_layout_orientation = OrientationHorizontal;
    p->desktop_layout_corner = DesktopLayoutCornerTopLeft;
    p->desktop_layout_columns = p->desktop_layout_rows = 0;
    setDefaultProperties();
    p->properties = properties;
    p->properties2 = properties2;
    p->windowTypes = windowTypes;
    p->states = states;
    p->actions = actions;
    // force support for Supported and SupportingWMCheck for window managers
    p->properties |= (Supported | SupportingWMCheck);
    p->clientProperties = DesktopNames // the only thing that can be changed by clients
        | WMPing;                      // or they can reply to this
    p->clientProperties2 = WM2DesktopLayout;

    p->role = WindowManager;

    if (doActivate) {
        activate();
    }
}

root_info::root_info(xcb_connection_t* connection,
                     net::Properties properties,
                     net::Properties2 properties2,
                     int screen,
                     bool doActivate)
{
    p = new root_info_private;
    p->ref = 1;
    p->atoms = atomsForConnection(connection);

    p->name = nullptr;

    p->conn = connection;

    p->temp_buf = nullptr;
    p->temp_buf_size = 0;

    const xcb_setup_t* setup = xcb_get_setup(p->conn);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);

    if (screen != -1 && screen < setup->roots_len) {
        for (int i = 0; i < screen; i++) {
            xcb_screen_next(&it);
        }
    }

    p->root = it.data->root;
    p->rootSize.width = it.data->width_in_pixels;
    p->rootSize.height = it.data->height_in_pixels;

    p->supportwindow = XCB_WINDOW_NONE;
    p->number_of_desktops = p->current_desktop = 0;
    p->active = XCB_WINDOW_NONE;
    p->clients = p->stacking = p->virtual_roots = (xcb_window_t*)nullptr;
    p->clients_count = p->stacking_count = p->virtual_roots_count = 0;
    p->showing_desktop = false;
    p->desktop_layout_orientation = OrientationHorizontal;
    p->desktop_layout_corner = DesktopLayoutCornerTopLeft;
    p->desktop_layout_columns = p->desktop_layout_rows = 0;
    setDefaultProperties();
    p->clientProperties = properties;
    p->clientProperties2 = properties2;
    p->properties = net::Properties();
    p->properties2 = net::Properties2();
    p->windowTypes = window_type_mask();
    p->states = net::States();
    p->actions = net::Actions();

    p->role = Client;

    if (doActivate) {
        activate();
    }
}

// Copy an existing root_info object.

root_info::root_info(const root_info& rootinfo)
{
    p = rootinfo.p;
    p->ref++;
}

// Be gone with our root_info.

root_info::~root_info()
{
    refdec_nri(p);

    if (!p->ref) {
        delete p;
    }
}

void root_info::setDefaultProperties()
{
    p->properties = Supported | SupportingWMCheck;
    p->windowTypes = window_type_mask::normal | window_type_mask::desktop | window_type_mask::dock
        | window_type_mask::toolbar | window_type_mask::menu | window_type_mask::dialog;
    p->states = Modal | Sticky | MaxVert | MaxHoriz | Shaded | SkipTaskbar | KeepAbove;
    p->properties2 = net::Properties2();
    p->actions = net::Actions();
    p->clientProperties = net::Properties();
    p->clientProperties2 = net::Properties2();
}

void root_info::activate()
{
    if (p->role == WindowManager) {
        setSupported();
        update(p->clientProperties, p->clientProperties2);
    } else {
        update(p->clientProperties, p->clientProperties2);
    }
}

void root_info::setClientList(const xcb_window_t* windows, unsigned int count)
{
    assert(p->role == WindowManager);
    p->clients_count = count;

    delete[] p->clients;
    p->clients = nwindup(windows, count);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_CLIENT_LIST),
                        XCB_ATOM_WINDOW,
                        32,
                        p->clients_count,
                        (const void*)windows);
}

void root_info::setClientListStacking(const xcb_window_t* windows, unsigned int count)
{
    assert(p->role == WindowManager);

    p->stacking_count = count;
    delete[] p->stacking;
    p->stacking = nwindup(windows, count);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_CLIENT_LIST_STACKING),
                        XCB_ATOM_WINDOW,
                        32,
                        p->stacking_count,
                        (const void*)windows);
}

void root_info::setNumberOfDesktops(int numberOfDesktops)
{
    if (p->role == WindowManager) {
        p->number_of_desktops = numberOfDesktops;
        uint32_t const d = numberOfDesktops;
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->root,
                            p->atom(_NET_NUMBER_OF_DESKTOPS),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            (const void*)&d);
    } else {
        uint32_t const data[5] = {uint32_t(numberOfDesktops), 0, 0, 0, 0};

        send_client_message(p->conn,
                            netwm_sendevent_mask,
                            p->root,
                            p->root,
                            p->atom(_NET_NUMBER_OF_DESKTOPS),
                            data);
    }
}

void root_info::setCurrentDesktop(int desktop, bool ignore_viewport)
{
    if (p->role == WindowManager) {
        p->current_desktop = desktop;
        uint32_t d = p->current_desktop - 1;
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->root,
                            p->atom(_NET_CURRENT_DESKTOP),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            (const void*)&d);
    } else {
        // TODO(romangg): Still needed?
        /*
        if (!ignore_viewport && KX11Extras::mapViewport()) {
            KX11Extras::setCurrentDesktop(desktop);
            return;
        }
        */

        uint32_t const data[5] = {uint32_t(desktop - 1), 0, 0, 0, 0};
        send_client_message(
            p->conn, netwm_sendevent_mask, p->root, p->root, p->atom(_NET_CURRENT_DESKTOP), data);
    }
}

void root_info::setDesktopName(int desktop, const char* desktopName)
{
    // Allow setting desktop names even for non-existent desktops, see the spec, sect.3.7.
    if (desktop < 1) {
        return;
    }

    delete[] p->desktop_names[desktop - 1];
    p->desktop_names[desktop - 1] = nstrdup(desktopName);

    unsigned int i;
    unsigned int proplen;
    unsigned int num
        = ((p->number_of_desktops > p->desktop_names.size()) ? p->number_of_desktops
                                                             : p->desktop_names.size());
    for (i = 0, proplen = 0; i < num; i++) {
        proplen += (p->desktop_names[i] != nullptr ? strlen(p->desktop_names[i]) + 1 : 1);
    }

    char* prop = new char[proplen];
    char* propp = prop;

    for (i = 0; i < num; i++) {
        if (p->desktop_names[i]) {
            strcpy(propp, p->desktop_names[i]);
            propp += strlen(p->desktop_names[i]) + 1;
        } else {
            *propp++ = '\0';
        }
    }

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_DESKTOP_NAMES),
                        p->atom(UTF8_STRING),
                        8,
                        proplen,
                        (const void*)prop);

    delete[] prop;
}

void root_info::setDesktopGeometry(const net::size& geometry)
{
    if (p->role == WindowManager) {
        p->geometry = geometry;

        uint32_t data[2];
        data[0] = p->geometry.width;
        data[1] = p->geometry.height;

        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->root,
                            p->atom(_NET_DESKTOP_GEOMETRY),
                            XCB_ATOM_CARDINAL,
                            32,
                            2,
                            (const void*)data);
    } else {
        uint32_t data[5] = {uint32_t(geometry.width), uint32_t(geometry.height), 0, 0, 0};

        send_client_message(
            p->conn, netwm_sendevent_mask, p->root, p->root, p->atom(_NET_DESKTOP_GEOMETRY), data);
    }
}

void root_info::setDesktopViewport(int desktop, const net::point& viewport)
{
    if (desktop < 1) {
        return;
    }

    if (p->role == WindowManager) {
        p->viewport[desktop - 1] = viewport;

        int d;
        int i;
        int l;
        l = p->number_of_desktops * 2;
        uint32_t* data = new uint32_t[l];
        for (d = 0, i = 0; d < p->number_of_desktops; d++) {
            data[i++] = p->viewport[d].x;
            data[i++] = p->viewport[d].y;
        }

        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->root,
                            p->atom(_NET_DESKTOP_VIEWPORT),
                            XCB_ATOM_CARDINAL,
                            32,
                            l,
                            (const void*)data);

        delete[] data;
    } else {
        const uint32_t data[5] = {uint32_t(viewport.x), uint32_t(viewport.y), 0, 0, 0};

        send_client_message(
            p->conn, netwm_sendevent_mask, p->root, p->root, p->atom(_NET_DESKTOP_VIEWPORT), data);
    }
}

void root_info::setSupported()
{
    assert(p->role == WindowManager);

    xcb_atom_t atoms[KwsAtomCount];
    int pnum = 2;

    // Root window properties/messages
    atoms[0] = p->atom(_NET_SUPPORTED);
    atoms[1] = p->atom(_NET_SUPPORTING_WM_CHECK);

    if (p->properties & ClientList) {
        atoms[pnum++] = p->atom(_NET_CLIENT_LIST);
    }

    if (p->properties & ClientListStacking) {
        atoms[pnum++] = p->atom(_NET_CLIENT_LIST_STACKING);
    }

    if (p->properties & NumberOfDesktops) {
        atoms[pnum++] = p->atom(_NET_NUMBER_OF_DESKTOPS);
    }

    if (p->properties & DesktopGeometry) {
        atoms[pnum++] = p->atom(_NET_DESKTOP_GEOMETRY);
    }

    if (p->properties & DesktopViewport) {
        atoms[pnum++] = p->atom(_NET_DESKTOP_VIEWPORT);
    }

    if (p->properties & CurrentDesktop) {
        atoms[pnum++] = p->atom(_NET_CURRENT_DESKTOP);
    }

    if (p->properties & DesktopNames) {
        atoms[pnum++] = p->atom(_NET_DESKTOP_NAMES);
    }

    if (p->properties & ActiveWindow) {
        atoms[pnum++] = p->atom(_NET_ACTIVE_WINDOW);
    }

    if (p->properties & WorkArea) {
        atoms[pnum++] = p->atom(_NET_WORKAREA);
    }

    if (p->properties & VirtualRoots) {
        atoms[pnum++] = p->atom(_NET_VIRTUAL_ROOTS);
    }

    if (p->properties2 & WM2DesktopLayout) {
        atoms[pnum++] = p->atom(_NET_DESKTOP_LAYOUT);
    }

    if (p->properties & CloseWindow) {
        atoms[pnum++] = p->atom(_NET_CLOSE_WINDOW);
    }

    if (p->properties2 & WM2RestackWindow) {
        atoms[pnum++] = p->atom(_NET_RESTACK_WINDOW);
    }

    if (p->properties2 & WM2ShowingDesktop) {
        atoms[pnum++] = p->atom(_NET_SHOWING_DESKTOP);
    }

    // Application window properties/messages
    if (p->properties & WMMoveResize) {
        atoms[pnum++] = p->atom(_NET_WM_MOVERESIZE);
    }

    if (p->properties2 & WM2MoveResizeWindow) {
        atoms[pnum++] = p->atom(_NET_MOVERESIZE_WINDOW);
    }

    if (p->properties & WMName) {
        atoms[pnum++] = p->atom(_NET_WM_NAME);
    }

    if (p->properties & WMVisibleName) {
        atoms[pnum++] = p->atom(_NET_WM_VISIBLE_NAME);
    }

    if (p->properties & WMIconName) {
        atoms[pnum++] = p->atom(_NET_WM_ICON_NAME);
    }

    if (p->properties & WMVisibleIconName) {
        atoms[pnum++] = p->atom(_NET_WM_VISIBLE_ICON_NAME);
    }

    if (p->properties & WMDesktop) {
        atoms[pnum++] = p->atom(_NET_WM_DESKTOP);
    }

    if (p->properties & WMWindowType) {
        atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE);

        // Application window types
        if (flags(p->windowTypes & window_type_mask::normal)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_NORMAL);
        }
        if (flags(p->windowTypes & window_type_mask::desktop)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_DESKTOP);
        }
        if (flags(p->windowTypes & window_type_mask::dock)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_DOCK);
        }
        if (flags(p->windowTypes & window_type_mask::toolbar)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_TOOLBAR);
        }
        if (flags(p->windowTypes & window_type_mask::menu)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_MENU);
        }
        if (flags(p->windowTypes & window_type_mask::dialog)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_DIALOG);
        }
        if (flags(p->windowTypes & window_type_mask::utility)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_UTILITY);
        }
        if (flags(p->windowTypes & window_type_mask::splash)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_SPLASH);
        }
        if (flags(p->windowTypes & window_type_mask::dropdown_menu)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_DROPDOWN_MENU);
        }
        if (flags(p->windowTypes & window_type_mask::popup_menu)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_POPUP_MENU);
        }
        if (flags(p->windowTypes & window_type_mask::tooltip)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_TOOLTIP);
        }
        if (flags(p->windowTypes & window_type_mask::notification)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_NOTIFICATION);
        }
        if (flags(p->windowTypes & window_type_mask::combo_box)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_COMBO);
        }
        if (flags(p->windowTypes & window_type_mask::dnd_icon)) {
            atoms[pnum++] = p->atom(_NET_WM_WINDOW_TYPE_DND);
        }
        // KDE extensions
        if (flags(p->windowTypes & window_type_mask::override)) {
            atoms[pnum++] = p->atom(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE);
        }
        if (flags(p->windowTypes & window_type_mask::top_menu)) {
            atoms[pnum++] = p->atom(_KDE_NET_WM_WINDOW_TYPE_TOPMENU);
        }
        if (flags(p->windowTypes & window_type_mask::on_screen_display)) {
            atoms[pnum++] = p->atom(_KDE_NET_WM_WINDOW_TYPE_ON_SCREEN_DISPLAY);
        }
        if (flags(p->windowTypes & window_type_mask::critical_notification)) {
            atoms[pnum++] = p->atom(_KDE_NET_WM_WINDOW_TYPE_CRITICAL_NOTIFICATION);
        }
        if (flags(p->windowTypes & window_type_mask::applet_popup)) {
            atoms[pnum++] = p->atom(_KDE_NET_WM_WINDOW_TYPE_APPLET_POPUP);
        }
    }

    if (p->properties & WMState) {
        atoms[pnum++] = p->atom(_NET_WM_STATE);

        // Application window states
        if (p->states & Modal) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_MODAL);
        }
        if (p->states & Sticky) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_STICKY);
        }
        if (p->states & MaxVert) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_MAXIMIZED_VERT);
        }
        if (p->states & MaxHoriz) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_MAXIMIZED_HORZ);
        }
        if (p->states & Shaded) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_SHADED);
        }
        if (p->states & SkipTaskbar) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_SKIP_TASKBAR);
        }
        if (p->states & SkipPager) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_SKIP_PAGER);
        }
        if (p->states & SkipSwitcher) {
            atoms[pnum++] = p->atom(_KDE_NET_WM_STATE_SKIP_SWITCHER);
        }
        if (p->states & Hidden) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_HIDDEN);
        }
        if (p->states & FullScreen) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_FULLSCREEN);
        }
        if (p->states & KeepAbove) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_ABOVE);
            // deprecated variant
            atoms[pnum++] = p->atom(_NET_WM_STATE_STAYS_ON_TOP);
        }
        if (p->states & KeepBelow) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_BELOW);
        }
        if (p->states & DemandsAttention) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_DEMANDS_ATTENTION);
        }

        if (p->states & Focused) {
            atoms[pnum++] = p->atom(_NET_WM_STATE_FOCUSED);
        }
    }

    if (p->properties & WMStrut) {
        atoms[pnum++] = p->atom(_NET_WM_STRUT);
    }

    if (p->properties2 & WM2ExtendedStrut) {
        atoms[pnum++] = p->atom(_NET_WM_STRUT_PARTIAL);
    }

    if (p->properties & WMIconGeometry) {
        atoms[pnum++] = p->atom(_NET_WM_ICON_GEOMETRY);
    }

    if (p->properties & WMIcon) {
        atoms[pnum++] = p->atom(_NET_WM_ICON);
    }

    if (p->properties & WMPid) {
        atoms[pnum++] = p->atom(_NET_WM_PID);
    }

    if (p->properties & WMHandledIcons) {
        atoms[pnum++] = p->atom(_NET_WM_HANDLED_ICONS);
    }

    if (p->properties & WMPing) {
        atoms[pnum++] = p->atom(_NET_WM_PING);
    }

    if (p->properties2 & WM2UserTime) {
        atoms[pnum++] = p->atom(_NET_WM_USER_TIME);
    }

    if (p->properties2 & WM2StartupId) {
        atoms[pnum++] = p->atom(_NET_STARTUP_ID);
    }

    if (p->properties2 & WM2Opacity) {
        atoms[pnum++] = p->atom(_NET_WM_WINDOW_OPACITY);
    }

    if (p->properties2 & WM2FullscreenMonitors) {
        atoms[pnum++] = p->atom(_NET_WM_FULLSCREEN_MONITORS);
    }

    if (p->properties2 & WM2AllowedActions) {
        atoms[pnum++] = p->atom(_NET_WM_ALLOWED_ACTIONS);

        // Actions
        if (p->actions & ActionMove) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_MOVE);
        }
        if (p->actions & ActionResize) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_RESIZE);
        }
        if (p->actions & ActionMinimize) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_MINIMIZE);
        }
        if (p->actions & ActionShade) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_SHADE);
        }
        if (p->actions & ActionStick) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_STICK);
        }
        if (p->actions & ActionMaxVert) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_MAXIMIZE_VERT);
        }
        if (p->actions & ActionMaxHoriz) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_MAXIMIZE_HORZ);
        }
        if (p->actions & ActionFullScreen) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_FULLSCREEN);
        }
        if (p->actions & ActionChangeDesktop) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_CHANGE_DESKTOP);
        }
        if (p->actions & ActionClose) {
            atoms[pnum++] = p->atom(_NET_WM_ACTION_CLOSE);
        }
    }

    if (p->properties & WMFrameExtents) {
        atoms[pnum++] = p->atom(_NET_FRAME_EXTENTS);
        atoms[pnum++] = p->atom(_KDE_NET_WM_FRAME_STRUT);
    }

    if (p->properties2 & WM2FrameOverlap) {
        atoms[pnum++] = p->atom(_NET_WM_FRAME_OVERLAP);
    }

    if (p->properties2 & WM2KDETemporaryRules) {
        atoms[pnum++] = p->atom(_KDE_NET_WM_TEMPORARY_RULES);
    }
    if (p->properties2 & WM2FullPlacement) {
        atoms[pnum++] = p->atom(_NET_WM_FULL_PLACEMENT);
    }

    if (p->properties2 & WM2Activities) {
        atoms[pnum++] = p->atom(_KDE_NET_WM_ACTIVITIES);
    }

    if (p->properties2 & WM2BlockCompositing) {
        atoms[pnum++] = p->atom(_KDE_NET_WM_BLOCK_COMPOSITING);
        atoms[pnum++] = p->atom(_NET_WM_BYPASS_COMPOSITOR);
    }

    if (p->properties2 & WM2KDEShadow) {
        atoms[pnum++] = p->atom(_KDE_NET_WM_SHADOW);
    }

    if (p->properties2 & WM2OpaqueRegion) {
        atoms[pnum++] = p->atom(_NET_WM_OPAQUE_REGION);
    }

    if (p->properties2 & WM2GTKFrameExtents) {
        atoms[pnum++] = p->atom(_GTK_FRAME_EXTENTS);
    }

    if (p->properties2 & WM2GTKShowWindowMenu) {
        atoms[pnum++] = p->atom(_GTK_SHOW_WINDOW_MENU);
    }

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_SUPPORTED),
                        XCB_ATOM_ATOM,
                        32,
                        pnum,
                        (const void*)atoms);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_SUPPORTING_WM_CHECK),
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        (const void*)&(p->supportwindow));

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->supportwindow,
                        p->atom(_NET_SUPPORTING_WM_CHECK),
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        (const void*)&(p->supportwindow));

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->supportwindow,
                        p->atom(_NET_WM_NAME),
                        p->atom(UTF8_STRING),
                        8,
                        strlen(p->name),
                        (const void*)p->name);
}

void root_info::updateSupportedProperties(xcb_atom_t atom)
{
    if (atom == p->atom(_NET_SUPPORTED)) {
        p->properties |= Supported;
    }

    else if (atom == p->atom(_NET_SUPPORTING_WM_CHECK)) {
        p->properties |= SupportingWMCheck;
    }

    else if (atom == p->atom(_NET_CLIENT_LIST)) {
        p->properties |= ClientList;
    }

    else if (atom == p->atom(_NET_CLIENT_LIST_STACKING)) {
        p->properties |= ClientListStacking;
    }

    else if (atom == p->atom(_NET_NUMBER_OF_DESKTOPS)) {
        p->properties |= NumberOfDesktops;
    }

    else if (atom == p->atom(_NET_DESKTOP_GEOMETRY)) {
        p->properties |= DesktopGeometry;
    }

    else if (atom == p->atom(_NET_DESKTOP_VIEWPORT)) {
        p->properties |= DesktopViewport;
    }

    else if (atom == p->atom(_NET_CURRENT_DESKTOP)) {
        p->properties |= CurrentDesktop;
    }

    else if (atom == p->atom(_NET_DESKTOP_NAMES)) {
        p->properties |= DesktopNames;
    }

    else if (atom == p->atom(_NET_ACTIVE_WINDOW)) {
        p->properties |= ActiveWindow;
    }

    else if (atom == p->atom(_NET_WORKAREA)) {
        p->properties |= WorkArea;
    }

    else if (atom == p->atom(_NET_VIRTUAL_ROOTS)) {
        p->properties |= VirtualRoots;
    }

    else if (atom == p->atom(_NET_DESKTOP_LAYOUT)) {
        p->properties2 |= WM2DesktopLayout;
    }

    else if (atom == p->atom(_NET_CLOSE_WINDOW)) {
        p->properties |= CloseWindow;
    }

    else if (atom == p->atom(_NET_RESTACK_WINDOW)) {
        p->properties2 |= WM2RestackWindow;
    }

    else if (atom == p->atom(_NET_SHOWING_DESKTOP)) {
        p->properties2 |= WM2ShowingDesktop;
    }

    // Application window properties/messages
    else if (atom == p->atom(_NET_WM_MOVERESIZE)) {
        p->properties |= WMMoveResize;
    }

    else if (atom == p->atom(_NET_MOVERESIZE_WINDOW)) {
        p->properties2 |= WM2MoveResizeWindow;
    }

    else if (atom == p->atom(_NET_WM_NAME)) {
        p->properties |= WMName;
    }

    else if (atom == p->atom(_NET_WM_VISIBLE_NAME)) {
        p->properties |= WMVisibleName;
    }

    else if (atom == p->atom(_NET_WM_ICON_NAME)) {
        p->properties |= WMIconName;
    }

    else if (atom == p->atom(_NET_WM_VISIBLE_ICON_NAME)) {
        p->properties |= WMVisibleIconName;
    }

    else if (atom == p->atom(_NET_WM_DESKTOP)) {
        p->properties |= WMDesktop;
    }

    else if (atom == p->atom(_NET_WM_WINDOW_TYPE)) {
        p->properties |= WMWindowType;
    }

    // Application window types
    else if (atom == p->atom(_NET_WM_WINDOW_TYPE_NORMAL)) {
        p->windowTypes |= window_type_mask::normal;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_DESKTOP)) {
        p->windowTypes |= window_type_mask::desktop;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_DOCK)) {
        p->windowTypes |= window_type_mask::dock;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_TOOLBAR)) {
        p->windowTypes |= window_type_mask::toolbar;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_MENU)) {
        p->windowTypes |= window_type_mask::menu;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_DIALOG)) {
        p->windowTypes |= window_type_mask::dialog;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_UTILITY)) {
        p->windowTypes |= window_type_mask::utility;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_SPLASH)) {
        p->windowTypes |= window_type_mask::splash;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_DROPDOWN_MENU)) {
        p->windowTypes |= window_type_mask::dropdown_menu;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_POPUP_MENU)) {
        p->windowTypes |= window_type_mask::popup_menu;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_TOOLTIP)) {
        p->windowTypes |= window_type_mask::tooltip;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_NOTIFICATION)) {
        p->windowTypes |= window_type_mask::notification;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_COMBO)) {
        p->windowTypes |= window_type_mask::combo_box;
    } else if (atom == p->atom(_NET_WM_WINDOW_TYPE_DND)) {
        p->windowTypes |= window_type_mask::dnd_icon;
    }
    // KDE extensions
    else if (atom == p->atom(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE)) {
        p->windowTypes |= window_type_mask::override;
    } else if (atom == p->atom(_KDE_NET_WM_WINDOW_TYPE_TOPMENU)) {
        p->windowTypes |= window_type_mask::top_menu;
    } else if (atom == p->atom(_KDE_NET_WM_WINDOW_TYPE_ON_SCREEN_DISPLAY)) {
        p->windowTypes |= window_type_mask::on_screen_display;
    } else if (atom == p->atom(_KDE_NET_WM_WINDOW_TYPE_CRITICAL_NOTIFICATION)) {
        p->windowTypes |= window_type_mask::critical_notification;
    } else if (atom == p->atom(_KDE_NET_WM_WINDOW_TYPE_APPLET_POPUP)) {
        p->windowTypes |= window_type_mask::applet_popup;
    }

    else if (atom == p->atom(_NET_WM_STATE)) {
        p->properties |= WMState;
    }

    // Application window states
    else if (atom == p->atom(_NET_WM_STATE_MODAL)) {
        p->states |= Modal;
    } else if (atom == p->atom(_NET_WM_STATE_STICKY)) {
        p->states |= Sticky;
    } else if (atom == p->atom(_NET_WM_STATE_MAXIMIZED_VERT)) {
        p->states |= MaxVert;
    } else if (atom == p->atom(_NET_WM_STATE_MAXIMIZED_HORZ)) {
        p->states |= MaxHoriz;
    } else if (atom == p->atom(_NET_WM_STATE_SHADED)) {
        p->states |= Shaded;
    } else if (atom == p->atom(_NET_WM_STATE_SKIP_TASKBAR)) {
        p->states |= SkipTaskbar;
    } else if (atom == p->atom(_NET_WM_STATE_SKIP_PAGER)) {
        p->states |= SkipPager;
    } else if (atom == p->atom(_KDE_NET_WM_STATE_SKIP_SWITCHER)) {
        p->states |= SkipSwitcher;
    } else if (atom == p->atom(_NET_WM_STATE_HIDDEN)) {
        p->states |= Hidden;
    } else if (atom == p->atom(_NET_WM_STATE_FULLSCREEN)) {
        p->states |= FullScreen;
    } else if (atom == p->atom(_NET_WM_STATE_ABOVE)) {
        p->states |= KeepAbove;
    } else if (atom == p->atom(_NET_WM_STATE_BELOW)) {
        p->states |= KeepBelow;
    } else if (atom == p->atom(_NET_WM_STATE_DEMANDS_ATTENTION)) {
        p->states |= DemandsAttention;
    } else if (atom == p->atom(_NET_WM_STATE_STAYS_ON_TOP)) {
        p->states |= KeepAbove;
    } else if (atom == p->atom(_NET_WM_STATE_FOCUSED)) {
        p->states |= Focused;
    }

    else if (atom == p->atom(_NET_WM_STRUT)) {
        p->properties |= WMStrut;
    }

    else if (atom == p->atom(_NET_WM_STRUT_PARTIAL)) {
        p->properties2 |= WM2ExtendedStrut;
    }

    else if (atom == p->atom(_NET_WM_ICON_GEOMETRY)) {
        p->properties |= WMIconGeometry;
    }

    else if (atom == p->atom(_NET_WM_ICON)) {
        p->properties |= WMIcon;
    }

    else if (atom == p->atom(_NET_WM_PID)) {
        p->properties |= WMPid;
    }

    else if (atom == p->atom(_NET_WM_HANDLED_ICONS)) {
        p->properties |= WMHandledIcons;
    }

    else if (atom == p->atom(_NET_WM_PING)) {
        p->properties |= WMPing;
    }

    else if (atom == p->atom(_NET_WM_USER_TIME)) {
        p->properties2 |= WM2UserTime;
    }

    else if (atom == p->atom(_NET_STARTUP_ID)) {
        p->properties2 |= WM2StartupId;
    }

    else if (atom == p->atom(_NET_WM_WINDOW_OPACITY)) {
        p->properties2 |= WM2Opacity;
    }

    else if (atom == p->atom(_NET_WM_FULLSCREEN_MONITORS)) {
        p->properties2 |= WM2FullscreenMonitors;
    }

    else if (atom == p->atom(_NET_WM_ALLOWED_ACTIONS)) {
        p->properties2 |= WM2AllowedActions;
    }

    // Actions
    else if (atom == p->atom(_NET_WM_ACTION_MOVE)) {
        p->actions |= ActionMove;
    } else if (atom == p->atom(_NET_WM_ACTION_RESIZE)) {
        p->actions |= ActionResize;
    } else if (atom == p->atom(_NET_WM_ACTION_MINIMIZE)) {
        p->actions |= ActionMinimize;
    } else if (atom == p->atom(_NET_WM_ACTION_SHADE)) {
        p->actions |= ActionShade;
    } else if (atom == p->atom(_NET_WM_ACTION_STICK)) {
        p->actions |= ActionStick;
    } else if (atom == p->atom(_NET_WM_ACTION_MAXIMIZE_VERT)) {
        p->actions |= ActionMaxVert;
    } else if (atom == p->atom(_NET_WM_ACTION_MAXIMIZE_HORZ)) {
        p->actions |= ActionMaxHoriz;
    } else if (atom == p->atom(_NET_WM_ACTION_FULLSCREEN)) {
        p->actions |= ActionFullScreen;
    } else if (atom == p->atom(_NET_WM_ACTION_CHANGE_DESKTOP)) {
        p->actions |= ActionChangeDesktop;
    } else if (atom == p->atom(_NET_WM_ACTION_CLOSE)) {
        p->actions |= ActionClose;
    }

    else if (atom == p->atom(_NET_FRAME_EXTENTS)) {
        p->properties |= WMFrameExtents;
    } else if (atom == p->atom(_KDE_NET_WM_FRAME_STRUT)) {
        p->properties |= WMFrameExtents;
    } else if (atom == p->atom(_NET_WM_FRAME_OVERLAP)) {
        p->properties2 |= WM2FrameOverlap;
    }

    else if (atom == p->atom(_KDE_NET_WM_TEMPORARY_RULES)) {
        p->properties2 |= WM2KDETemporaryRules;
    } else if (atom == p->atom(_NET_WM_FULL_PLACEMENT)) {
        p->properties2 |= WM2FullPlacement;
    }

    else if (atom == p->atom(_KDE_NET_WM_ACTIVITIES)) {
        p->properties2 |= WM2Activities;
    }

    else if (atom == p->atom(_KDE_NET_WM_BLOCK_COMPOSITING)
             || atom == p->atom(_NET_WM_BYPASS_COMPOSITOR)) {
        p->properties2 |= WM2BlockCompositing;
    }

    else if (atom == p->atom(_KDE_NET_WM_SHADOW)) {
        p->properties2 |= WM2KDEShadow;
    }

    else if (atom == p->atom(_NET_WM_OPAQUE_REGION)) {
        p->properties2 |= WM2OpaqueRegion;
    }

    else if (atom == p->atom(_GTK_FRAME_EXTENTS)) {
        p->properties2 |= WM2GTKFrameExtents;
    }

    else if (atom == p->atom(_GTK_SHOW_WINDOW_MENU)) {
        p->properties2 |= WM2GTKShowWindowMenu;
    }

    else if (atom == p->atom(_KDE_NET_WM_APPMENU_OBJECT_PATH)) {
        p->properties2 |= WM2AppMenuObjectPath;
    }

    else if (atom == p->atom(_KDE_NET_WM_APPMENU_SERVICE_NAME)) {
        p->properties2 |= WM2AppMenuServiceName;
    }
}

void root_info::setActiveWindow(xcb_window_t window)
{
    setActiveWindow(window, FromUnknown, QX11Info::appUserTime(), XCB_WINDOW_NONE);
}

void root_info::setActiveWindow(xcb_window_t window,
                                net::RequestSource src,
                                xcb_timestamp_t timestamp,
                                xcb_window_t active_window)
{
    p->active = window;

    if (p->role == WindowManager) {
        p->active = window;

        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->root,
                            p->atom(_NET_ACTIVE_WINDOW),
                            XCB_ATOM_WINDOW,
                            32,
                            1,
                            (const void*)&(p->active));
    } else {
        const uint32_t data[5] = {src, timestamp, active_window, 0, 0};

        send_client_message(
            p->conn, netwm_sendevent_mask, p->root, window, p->atom(_NET_ACTIVE_WINDOW), data);
    }
}

void root_info::setWorkArea(int desktop, const net::rect& workarea)
{
    assert(p->role == WindowManager);

    if (desktop < 1) {
        return;
    }

    p->workarea[desktop - 1] = workarea;

    uint32_t* wa = new uint32_t[p->number_of_desktops * 4];
    int i;
    int o;
    for (i = 0, o = 0; i < p->number_of_desktops; i++) {
        wa[o++] = p->workarea[i].pos.x;
        wa[o++] = p->workarea[i].pos.y;
        wa[o++] = p->workarea[i].size.width;
        wa[o++] = p->workarea[i].size.height;
    }

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_WORKAREA),
                        XCB_ATOM_CARDINAL,
                        32,
                        p->number_of_desktops * 4,
                        (const void*)wa);

    delete[] wa;
}

void root_info::setVirtualRoots(const xcb_window_t* windows, unsigned int count)
{
    assert(p->role == WindowManager);

    p->virtual_roots_count = count;
    delete[] p->virtual_roots;
    p->virtual_roots = nwindup(windows, count);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_VIRTUAL_ROOTS),
                        XCB_ATOM_WINDOW,
                        32,
                        p->virtual_roots_count,
                        (const void*)windows);
}

void root_info::setDesktopLayout(net::Orientation orientation,
                                 int columns,
                                 int rows,
                                 net::DesktopLayoutCorner corner)
{
    p->desktop_layout_orientation = orientation;
    p->desktop_layout_columns = columns;
    p->desktop_layout_rows = rows;
    p->desktop_layout_corner = corner;

    uint32_t data[4];
    data[0] = orientation;
    data[1] = columns;
    data[2] = rows;
    data[3] = corner;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->root,
                        p->atom(_NET_DESKTOP_LAYOUT),
                        XCB_ATOM_CARDINAL,
                        32,
                        4,
                        (const void*)data);
}

void root_info::setShowingDesktop(bool showing)
{
    if (p->role == WindowManager) {
        uint32_t d = p->showing_desktop = showing;
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->root,
                            p->atom(_NET_SHOWING_DESKTOP),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            (const void*)&d);
    } else {
        uint32_t data[5] = {uint32_t(showing ? 1 : 0), 0, 0, 0, 0};
        send_client_message(
            p->conn, netwm_sendevent_mask, p->root, p->root, p->atom(_NET_SHOWING_DESKTOP), data);
    }
}

bool root_info::showingDesktop() const
{
    return p->showing_desktop;
}

void root_info::closeWindowRequest(xcb_window_t window)
{
    uint32_t const data[5] = {0, 0, 0, 0, 0};
    send_client_message(
        p->conn, netwm_sendevent_mask, p->root, window, p->atom(_NET_CLOSE_WINDOW), data);
}

void root_info::moveResizeRequest(xcb_window_t window, int x_root, int y_root, Direction direction)
{
    uint32_t const data[5] = {uint32_t(x_root), uint32_t(y_root), uint32_t(direction), 0, 0};

    send_client_message(
        p->conn, netwm_sendevent_mask, p->root, window, p->atom(_NET_WM_MOVERESIZE), data);
}

void root_info::moveResizeWindowRequest(xcb_window_t window,
                                        int flags,
                                        int x,
                                        int y,
                                        int width,
                                        int height)
{
    uint32_t const data[5]
        = {uint32_t(flags), uint32_t(x), uint32_t(y), uint32_t(width), uint32_t(height)};

    send_client_message(
        p->conn, netwm_sendevent_mask, p->root, window, p->atom(_NET_MOVERESIZE_WINDOW), data);
}

void root_info::showWindowMenuRequest(xcb_window_t window, int device_id, int x_root, int y_root)
{
    uint32_t const data[5] = {uint32_t(device_id), uint32_t(x_root), uint32_t(y_root), 0, 0};
    send_client_message(
        p->conn, netwm_sendevent_mask, p->root, window, p->atom(_GTK_SHOW_WINDOW_MENU), data);
}

void root_info::restackRequest(xcb_window_t window,
                               RequestSource src,
                               xcb_window_t above,
                               int detail,
                               xcb_timestamp_t timestamp)
{
    uint32_t const data[5]
        = {uint32_t(src), uint32_t(above), uint32_t(detail), uint32_t(timestamp), 0};

    send_client_message(
        p->conn, netwm_sendevent_mask, p->root, window, p->atom(_NET_RESTACK_WINDOW), data);
}

void root_info::sendPing(xcb_window_t window, xcb_timestamp_t timestamp)
{
    assert(p->role == WindowManager);

    uint32_t const data[5] = {p->atom(_NET_WM_PING), timestamp, window, 0, 0};
    send_client_message(p->conn, 0, window, window, p->atom(WM_PROTOCOLS), data);
}

// assignment operator

const root_info& root_info::operator=(const root_info& rootinfo)
{
    if (p != rootinfo.p) {
        refdec_nri(p);

        if (!p->ref) {
            delete p;
        }
    }

    p = rootinfo.p;
    p->ref++;

    return *this;
}

net::Properties root_info::event(xcb_generic_event_t* ev)
{
    net::Properties props;
    event(ev, &props);
    return props;
}

void root_info::event(xcb_generic_event_t* event,
                      net::Properties* properties,
                      net::Properties2* properties2)
{
    net::Properties dirty;
    net::Properties2 dirty2;
    bool do_update = false;
    const uint8_t eventType = event->response_type & ~0x80;

    // the window manager will be interested in client messages... no other
    // client should get these messages
    if (p->role == WindowManager && eventType == XCB_CLIENT_MESSAGE
        && reinterpret_cast<xcb_client_message_event_t*>(event)->format == 32) {
        auto message = reinterpret_cast<xcb_client_message_event_t*>(event);

        if (message->type == p->atom(_NET_NUMBER_OF_DESKTOPS)) {
            dirty = NumberOfDesktops;
            changeNumberOfDesktops(message->data.data32[0]);
        } else if (message->type == p->atom(_NET_DESKTOP_GEOMETRY)) {
            dirty = DesktopGeometry;

            net::size sz;
            sz.width = message->data.data32[0];
            sz.height = message->data.data32[1];
            changeDesktopGeometry(~0, sz);
        } else if (message->type == p->atom(_NET_DESKTOP_VIEWPORT)) {
            dirty = DesktopViewport;

            net::point pt;
            pt.x = message->data.data32[0];
            pt.y = message->data.data32[1];
            changeDesktopViewport(p->current_desktop, pt);
        } else if (message->type == p->atom(_NET_CURRENT_DESKTOP)) {
            dirty = CurrentDesktop;
            changeCurrentDesktop(message->data.data32[0] + 1);
        } else if (message->type == p->atom(_NET_ACTIVE_WINDOW)) {
            dirty = ActiveWindow;
            RequestSource src = FromUnknown;
            xcb_timestamp_t timestamp = XCB_TIME_CURRENT_TIME;
            xcb_window_t active_window = XCB_WINDOW_NONE;
            // make sure there aren't unknown values
            if (message->data.data32[0] >= FromUnknown && message->data.data32[0] <= FromTool) {
                src = static_cast<RequestSource>(message->data.data32[0]);
                timestamp = message->data.data32[1];
                active_window = message->data.data32[2];
            }
            changeActiveWindow(message->window, src, timestamp, active_window);
        } else if (message->type == p->atom(_NET_WM_MOVERESIZE)) {
            moveResize(message->window,
                       message->data.data32[0],
                       message->data.data32[1],
                       message->data.data32[2]);
        } else if (message->type == p->atom(_NET_MOVERESIZE_WINDOW)) {
            moveResizeWindow(message->window,
                             message->data.data32[0],
                             message->data.data32[1],
                             message->data.data32[2],
                             message->data.data32[3],
                             message->data.data32[4]);
        } else if (message->type == p->atom(_NET_CLOSE_WINDOW)) {
            closeWindow(message->window);
        } else if (message->type == p->atom(_NET_RESTACK_WINDOW)) {
            RequestSource src = FromUnknown;
            xcb_timestamp_t timestamp = XCB_TIME_CURRENT_TIME;
            // make sure there aren't unknown values
            if (message->data.data32[0] >= FromUnknown && message->data.data32[0] <= FromTool) {
                src = static_cast<RequestSource>(message->data.data32[0]);
                timestamp = message->data.data32[3];
            }
            restackWindow(
                message->window, src, message->data.data32[1], message->data.data32[2], timestamp);
        } else if (message->type == p->atom(WM_PROTOCOLS)
                   && (xcb_atom_t)message->data.data32[0] == p->atom(_NET_WM_PING)) {
            dirty = WMPing;
            gotPing(message->data.data32[2], message->data.data32[1]);
        } else if (message->type == p->atom(_NET_SHOWING_DESKTOP)) {
            dirty2 = WM2ShowingDesktop;
            changeShowingDesktop(message->data.data32[0]);
        } else if (message->type == p->atom(_GTK_SHOW_WINDOW_MENU)) {
            showWindowMenu(message->window,
                           message->data.data32[0],
                           message->data.data32[1],
                           message->data.data32[2]);
        }
    }

    if (eventType == XCB_PROPERTY_NOTIFY) {
        xcb_property_notify_event_t* pe = reinterpret_cast<xcb_property_notify_event_t*>(event);
        if (pe->atom == p->atom(_NET_CLIENT_LIST)) {
            dirty |= ClientList;
        } else if (pe->atom == p->atom(_NET_CLIENT_LIST_STACKING)) {
            dirty |= ClientListStacking;
        } else if (pe->atom == p->atom(_NET_DESKTOP_NAMES)) {
            dirty |= DesktopNames;
        } else if (pe->atom == p->atom(_NET_WORKAREA)) {
            dirty |= WorkArea;
        } else if (pe->atom == p->atom(_NET_NUMBER_OF_DESKTOPS)) {
            dirty |= NumberOfDesktops;
        } else if (pe->atom == p->atom(_NET_DESKTOP_GEOMETRY)) {
            dirty |= DesktopGeometry;
        } else if (pe->atom == p->atom(_NET_DESKTOP_VIEWPORT)) {
            dirty |= DesktopViewport;
        } else if (pe->atom == p->atom(_NET_CURRENT_DESKTOP)) {
            dirty |= CurrentDesktop;
        } else if (pe->atom == p->atom(_NET_ACTIVE_WINDOW)) {
            dirty |= ActiveWindow;
        } else if (pe->atom == p->atom(_NET_SHOWING_DESKTOP)) {
            dirty2 |= WM2ShowingDesktop;
        } else if (pe->atom == p->atom(_NET_SUPPORTED)) {
            dirty |= Supported; // update here?
        } else if (pe->atom == p->atom(_NET_SUPPORTING_WM_CHECK)) {
            dirty |= SupportingWMCheck;
        } else if (pe->atom == p->atom(_NET_VIRTUAL_ROOTS)) {
            dirty |= VirtualRoots;
        } else if (pe->atom == p->atom(_NET_DESKTOP_LAYOUT)) {
            dirty2 |= WM2DesktopLayout;
        }

        do_update = true;
    }

    if (do_update) {
        update(dirty, dirty2);
    }

    if (properties) {
        *properties = dirty;
    }
    if (properties2) {
        *properties2 = dirty2;
    }
}

// private functions to update the data we keep

void root_info::update(net::Properties properties, net::Properties2 properties2)
{
    net::Properties dirty = properties & p->clientProperties;
    net::Properties2 dirty2 = properties2 & p->clientProperties2;

    xcb_get_property_cookie_t cookies[255];
    xcb_get_property_cookie_t wm_name_cookie;
    int c = 0;

    // Send the property requests
    if (dirty & Supported) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_SUPPORTED), XCB_ATOM_ATOM, 0, MAX_PROP_SIZE);
    }

    if (dirty & ClientList) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_CLIENT_LIST), XCB_ATOM_WINDOW, 0, MAX_PROP_SIZE);
    }

    if (dirty & ClientListStacking) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->root,
                                        p->atom(_NET_CLIENT_LIST_STACKING),
                                        XCB_ATOM_WINDOW,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & NumberOfDesktops) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_NUMBER_OF_DESKTOPS), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty & DesktopGeometry) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_DESKTOP_GEOMETRY), XCB_ATOM_CARDINAL, 0, 2);
    }

    if (dirty & DesktopViewport) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->root,
                                        p->atom(_NET_DESKTOP_VIEWPORT),
                                        XCB_ATOM_CARDINAL,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & CurrentDesktop) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_CURRENT_DESKTOP), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty & DesktopNames) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->root,
                                        p->atom(_NET_DESKTOP_NAMES),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & ActiveWindow) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_ACTIVE_WINDOW), XCB_ATOM_WINDOW, 0, 1);
    }

    if (dirty & WorkArea) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_WORKAREA), XCB_ATOM_CARDINAL, 0, MAX_PROP_SIZE);
    }

    if (dirty & SupportingWMCheck) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_SUPPORTING_WM_CHECK), XCB_ATOM_WINDOW, 0, 1);
    }

    if (dirty & VirtualRoots) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_VIRTUAL_ROOTS), XCB_ATOM_WINDOW, 0, 1);
    }

    if (dirty2 & WM2DesktopLayout) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->root,
                                        p->atom(_NET_DESKTOP_LAYOUT),
                                        XCB_ATOM_CARDINAL,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2ShowingDesktop) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->root, p->atom(_NET_SHOWING_DESKTOP), XCB_ATOM_CARDINAL, 0, 1);
    }

    // Get the replies
    c = 0;

    if (dirty & Supported) {
        // Only in Client mode
        p->properties = net::Properties();
        p->properties2 = net::Properties2();
        p->windowTypes = window_type_mask();
        p->states = net::States();
        p->actions = net::Actions();

        auto atoms = get_array_reply<xcb_atom_t>(p->conn, cookies[c++], XCB_ATOM_ATOM);
        for (const xcb_atom_t atom : atoms) {
            updateSupportedProperties(atom);
        }
    }

    if (dirty & ClientList) {
        QList<xcb_window_t> clientsToRemove;
        QList<xcb_window_t> clientsToAdd;

        QVector<xcb_window_t> clients
            = get_array_reply<xcb_window_t>(p->conn, cookies[c++], XCB_ATOM_WINDOW);
        std::sort(clients.begin(), clients.end());

        if (p->clients) {
            if (p->role == Client) {
                int new_index = 0;
                int old_index = 0;
                int old_count = p->clients_count;
                int new_count = clients.count();

                while (old_index < old_count || new_index < new_count) {
                    if (old_index == old_count) {
                        clientsToAdd.append(clients[new_index++]);
                    } else if (new_index == new_count) {
                        clientsToRemove.append(p->clients[old_index++]);
                    } else {
                        if (p->clients[old_index] < clients[new_index]) {
                            clientsToRemove.append(p->clients[old_index++]);
                        } else if (clients[new_index] < p->clients[old_index]) {
                            clientsToAdd.append(clients[new_index++]);
                        } else {
                            new_index++;
                            old_index++;
                        }
                    }
                }
            }

            delete[] p->clients;
            p->clients = nullptr;
        } else {
            clientsToAdd.reserve(clients.count());
            for (int i = 0; i < clients.count(); i++) {
                clientsToAdd.append(clients[i]);
            }
        }

        if (!clients.isEmpty()) {
            p->clients_count = clients.count();
            p->clients = new xcb_window_t[clients.count()];
            for (int i = 0; i < clients.count(); i++) {
                p->clients[i] = clients.at(i);
            }
        }

        for (int i = 0; i < clientsToRemove.size(); ++i) {
            removeClient(clientsToRemove.at(i));
        }

        for (int i = 0; i < clientsToAdd.size(); ++i) {
            addClient(clientsToAdd.at(i));
        }
    }

    if (dirty & ClientListStacking) {
        p->stacking_count = 0;

        delete[] p->stacking;
        p->stacking = nullptr;

        const QVector<xcb_window_t> wins
            = get_array_reply<xcb_window_t>(p->conn, cookies[c++], XCB_ATOM_WINDOW);

        if (!wins.isEmpty()) {
            p->stacking_count = wins.count();
            p->stacking = new xcb_window_t[wins.count()];
            for (int i = 0; i < wins.count(); i++) {
                p->stacking[i] = wins.at(i);
            }
        }
    }

    if (dirty & NumberOfDesktops) {
        p->number_of_desktops
            = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0);
    }

    if (dirty & DesktopGeometry) {
        p->geometry = p->rootSize;

        const QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 2) {
            p->geometry.width = data.at(0);
            p->geometry.height = data.at(1);
        }
    }

    if (dirty & DesktopViewport) {
        for (int i = 0; i < p->viewport.size(); i++) {
            p->viewport[i].x = p->viewport[i].y = 0;
        }

        const QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);

        if (data.count() >= 2) {
            int n = data.count() / 2;
            for (int d = 0, i = 0; d < n; d++) {
                p->viewport[d].x = data[i++];
                p->viewport[d].y = data[i++];
            }
        }
    }

    if (dirty & CurrentDesktop) {
        p->current_desktop
            = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0) + 1;
    }

    if (dirty & DesktopNames) {
        for (int i = 0; i < p->desktop_names.size(); ++i) {
            delete[] p->desktop_names[i];
        }

        p->desktop_names.reset();

        const QList<QByteArray> names
            = get_stringlist_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        for (int i = 0; i < names.count(); i++) {
            p->desktop_names[i] = nstrndup(names[i].constData(), names[i].length());
        }
    }

    if (dirty & ActiveWindow) {
        p->active = get_value_reply<xcb_window_t>(p->conn, cookies[c++], XCB_ATOM_WINDOW, 0);
    }

    if (dirty & WorkArea) {
        p->workarea.reset();

        const QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == p->number_of_desktops * 4) {
            for (int i = 0, j = 0; i < p->number_of_desktops; i++) {
                p->workarea[i].pos.x = data[j++];
                p->workarea[i].pos.y = data[j++];
                p->workarea[i].size.width = data[j++];
                p->workarea[i].size.height = data[j++];
            }
        }
    }

    if (dirty & SupportingWMCheck) {
        delete[] p->name;
        p->name = nullptr;

        p->supportwindow = get_value_reply<xcb_window_t>(p->conn, cookies[c++], XCB_ATOM_WINDOW, 0);

        // We'll get the reply for this request at the bottom of this function,
        // after we've processing the other pending replies
        if (p->supportwindow) {
            wm_name_cookie = xcb_get_property(p->conn,
                                              false,
                                              p->supportwindow,
                                              p->atom(_NET_WM_NAME),
                                              p->atom(UTF8_STRING),
                                              0,
                                              MAX_PROP_SIZE);
        }
    }

    if (dirty & VirtualRoots) {
        p->virtual_roots_count = 0;

        delete[] p->virtual_roots;
        p->virtual_roots = nullptr;

        const QVector<xcb_window_t> wins
            = get_array_reply<xcb_window_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);

        if (!wins.isEmpty()) {
            p->virtual_roots_count = wins.count();
            p->virtual_roots = new xcb_window_t[wins.count()];
            for (int i = 0; i < wins.count(); i++) {
                p->virtual_roots[i] = wins.at(i);
            }
        }
    }

    if (dirty2 & WM2DesktopLayout) {
        p->desktop_layout_orientation = OrientationHorizontal;
        p->desktop_layout_corner = DesktopLayoutCornerTopLeft;
        p->desktop_layout_columns = p->desktop_layout_rows = 0;

        const QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);

        if (data.count() >= 4 && data[3] <= 3) {
            p->desktop_layout_corner = (net::DesktopLayoutCorner)data[3];
        }

        if (data.count() >= 3) {
            if (data[0] <= 1) {
                p->desktop_layout_orientation = (net::Orientation)data[0];
            }

            p->desktop_layout_columns = data[1];
            p->desktop_layout_rows = data[2];
        }
    }

    if (dirty2 & WM2ShowingDesktop) {
        uint32_t const val = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0);
        p->showing_desktop = bool(val);
    }

    if ((dirty & SupportingWMCheck) && p->supportwindow) {
        const QByteArray ba = get_string_reply(p->conn, wm_name_cookie, p->atom(UTF8_STRING));
        if (ba.length() > 0) {
            p->name = nstrndup((const char*)ba.constData(), ba.length());
        }
    }
}

xcb_connection_t* root_info::xcbConnection() const
{
    return p->conn;
}

xcb_window_t root_info::rootWindow() const
{
    return p->root;
}

xcb_window_t root_info::supportWindow() const
{
    return p->supportwindow;
}

const char* root_info::wmName() const
{
    return p->name;
}

net::Properties root_info::supportedProperties() const
{
    return p->properties;
}

net::Properties2 root_info::supportedProperties2() const
{
    return p->properties2;
}

net::States root_info::supportedStates() const
{
    return p->states;
}

window_type_mask root_info::supportedWindowTypes() const
{
    return p->windowTypes;
}

net::Actions root_info::supportedActions() const
{
    return p->actions;
}

net::Properties root_info::passedProperties() const
{
    return p->role == WindowManager ? p->properties : p->clientProperties;
}

net::Properties2 root_info::passedProperties2() const
{
    return p->role == WindowManager ? p->properties2 : p->clientProperties2;
}

net::States root_info::passedStates() const
{
    return p->role == WindowManager ? p->states : net::States();
}

window_type_mask root_info::passedWindowTypes() const
{
    return p->role == WindowManager ? p->windowTypes : window_type_mask();
}

net::Actions root_info::passedActions() const
{
    return p->role == WindowManager ? p->actions : net::Actions();
}

void root_info::setSupported(net::Property property, bool on)
{
    assert(p->role == WindowManager);

    if (on && !isSupported(property)) {
        p->properties |= property;
        setSupported();
    } else if (!on && isSupported(property)) {
        p->properties &= ~property;
        setSupported();
    }
}

void root_info::setSupported(net::Property2 property, bool on)
{
    assert(p->role == WindowManager);

    if (on && !isSupported(property)) {
        p->properties2 |= property;
        setSupported();
    } else if (!on && isSupported(property)) {
        p->properties2 &= ~property;
        setSupported();
    }
}

void root_info::setSupported(window_type_mask property, bool on)
{
    assert(p->role == WindowManager);

    if (on && !isSupported(property)) {
        p->windowTypes |= property;
        setSupported();
    } else if (!on && isSupported(property)) {
        p->windowTypes &= ~property;
        setSupported();
    }
}

void root_info::setSupported(net::State property, bool on)
{
    assert(p->role == WindowManager);

    if (on && !isSupported(property)) {
        p->states |= property;
        setSupported();
    } else if (!on && isSupported(property)) {
        p->states &= ~property;
        setSupported();
    }
}

void root_info::setSupported(net::Action property, bool on)
{
    assert(p->role == WindowManager);

    if (on && !isSupported(property)) {
        p->actions |= property;
        setSupported();
    } else if (!on && isSupported(property)) {
        p->actions &= ~property;
        setSupported();
    }
}

bool root_info::isSupported(net::Property property) const
{
    return p->properties & property;
}

bool root_info::isSupported(net::Property2 property) const
{
    return p->properties2 & property;
}

bool root_info::isSupported(window_type_mask type) const
{
    return flags(p->windowTypes & type);
}

bool root_info::isSupported(net::State state) const
{
    return p->states & state;
}

bool root_info::isSupported(net::Action action) const
{
    return p->actions & action;
}

const xcb_window_t* root_info::clientList() const
{
    return p->clients;
}

int root_info::clientListCount() const
{
    return p->clients_count;
}

const xcb_window_t* root_info::clientListStacking() const
{
    return p->stacking;
}

int root_info::clientListStackingCount() const
{
    return p->stacking_count;
}

net::size root_info::desktopGeometry() const
{
    return p->geometry.width != 0 ? p->geometry : p->rootSize;
}

net::point root_info::desktopViewport(int desktop) const
{
    if (desktop < 1) {
        // set to (0,0)
        return net::point();
    }

    return p->viewport[desktop - 1];
}

net::rect root_info::workArea(int desktop) const
{
    if (desktop < 1) {
        return net::rect();
    }

    return p->workarea[desktop - 1];
}

const char* root_info::desktopName(int desktop) const
{
    if (desktop < 1) {
        return nullptr;
    }

    return p->desktop_names[desktop - 1];
}

const xcb_window_t* root_info::virtualRoots() const
{
    return p->virtual_roots;
}

int root_info::virtualRootsCount() const
{
    return p->virtual_roots_count;
}

net::Orientation root_info::desktopLayoutOrientation() const
{
    return p->desktop_layout_orientation;
}

QSize root_info::desktopLayoutColumnsRows() const
{
    return QSize(p->desktop_layout_columns, p->desktop_layout_rows);
}

net::DesktopLayoutCorner root_info::desktopLayoutCorner() const
{
    return p->desktop_layout_corner;
}

xcb_window_t root_info::activeWindow() const
{
    return p->active;
}

void root_info::virtual_hook(int, void*)
{
    /*BASE::virtual_hook( id, data );*/
}

}
