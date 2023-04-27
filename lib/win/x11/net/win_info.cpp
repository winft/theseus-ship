/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "win_info.h"

#include "info_p.h"
#include "rarray.h"

#include "win/x11/extras.h"

// For devicePixelRatio
// TODO(romangg): remove!
#include <QGuiApplication>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xproto.h>

namespace KWin::win::x11::net
{

// This struct is defined here to avoid a dependency on xcb-icccm
struct kde_wm_hints {
    uint32_t flags;
    uint32_t input;
    int32_t initial_state;
    xcb_pixmap_t icon_pixmap;
    xcb_window_t icon_window;
    int32_t icon_x;
    int32_t icon_y;
    xcb_pixmap_t icon_mask;
    xcb_window_t window_group;
};

struct win_info_private {
    net::Role role;

    xcb_connection_t* conn;
    xcb_window_t window, root;
    net::MappingState mapping_state;
    bool mapping_state_dirty;

    rarray<net::icon> icons;
    int icon_count;
    int* icon_sizes; // for iconSizes() only

    net::rect icon_geom, win_geom;
    net::States state;
    net::extended_strut extended_strut;
    net::strut strut;
    net::strut frame_strut; // strut?
    net::strut frame_overlap;
    net::strut gtk_frame_extents;
    rarray<win_type> types;
    char *name, *visible_name, *icon_name, *visible_icon_name;
    int desktop;
    int pid;
    bool handled_icons;
    xcb_timestamp_t user_time;
    char* startup_id;
    unsigned long opacity;
    xcb_window_t transient_for, window_group;
    xcb_pixmap_t icon_pixmap, icon_mask;
    net::Actions allowed_actions;
    char *class_class, *class_name, *window_role, *client_machine, *desktop_file,
        *appmenu_object_path, *appmenu_service_name, *gtk_application_id;

    net::Properties properties;
    net::Properties2 properties2;
    net::fullscreen_monitors fullscreen_monitors;
    bool has_net_support;

    bool blockCompositing;
    bool urgency;
    bool input;
    net::MappingState initialMappingState;
    net::Protocols protocols;
    std::vector<net::rect> opaqueRegion;

    int ref;

    QSharedDataPointer<Atoms> atoms;
    xcb_atom_t atom(KwsAtom atom) const
    {
        return atoms->atom(atom);
    }
};

static const uint32_t netwm_sendevent_mask
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

static void refdec_nwi(win_info_private* p)
{
    if (!--p->ref) {
        delete[] p->name;
        delete[] p->visible_name;
        delete[] p->window_role;
        delete[] p->icon_name;
        delete[] p->visible_icon_name;
        delete[] p->startup_id;
        delete[] p->class_class;
        delete[] p->class_name;
        delete[] p->client_machine;
        delete[] p->desktop_file;
        delete[] p->gtk_application_id;
        delete[] p->appmenu_object_path;
        delete[] p->appmenu_service_name;

        int i;
        for (i = 0; i < p->icons.size(); i++) {
            delete[] p->icons[i].data;
        }
        delete[] p->icon_sizes;
    }
}

static void readIcon(xcb_connection_t* c,
                     const xcb_get_property_cookie_t cookie,
                     net::rarray<net::icon>& icons,
                     int& icon_count)
{
    // reset
    for (int i = 0; i < icons.size(); i++) {
        delete[] icons[i].data;
    }

    icons.reset();
    icon_count = 0;

    xcb_get_property_reply_t* reply = xcb_get_property_reply(c, cookie, nullptr);

    if (!reply || reply->value_len < 3 || reply->format != 32 || reply->type != XCB_ATOM_CARDINAL) {
        if (reply) {
            free(reply);
        }

        return;
    }

    uint32_t* data = (uint32_t*)xcb_get_property_value(reply);

    for (unsigned int i = 0, j = 0; j < reply->value_len - 2; i++) {
        uint32_t width = data[j++];
        uint32_t height = data[j++];
        uint32_t size = width * height * sizeof(uint32_t);
        if (j + width * height > reply->value_len) {
            fprintf(stderr,
                    "Ill-encoded icon data; proposed size leads to out of bounds access. Skipping. "
                    "(%d x %d)\n",
                    width,
                    height);
            break;
        }
        if (width > 1024 || height > 1024) {
            fprintf(stderr,
                    "Warning: found huge icon. The icon data may be ill-encoded. (%d x %d)\n",
                    width,
                    height);
            // do not break nor continue - the data may likely be junk, but causes no harm (yet) and
            // might actually be just a huge icon, eg. when the icon system is abused to transfer
            // wallpapers or such.
        }

        icons[i].size.width = width;
        icons[i].size.height = height;
        icons[i].data = new unsigned char[size];

        memcpy((void*)icons[i].data, (const void*)&data[j], size);

        j += width * height;
        icon_count++;
    }

    free(reply);
}

const int win_info::OnAllDesktops = net::OnAllDesktops;

win_info::win_info(xcb_connection_t* connection,
                   xcb_window_t window,
                   xcb_window_t rootWindow,
                   net::Properties properties,
                   net::Properties2 properties2,
                   Role role)
{
    p = new win_info_private;
    p->ref = 1;
    p->atoms = atomsForConnection(connection);

    p->conn = connection;
    p->window = window;
    p->root = rootWindow;
    p->mapping_state = Withdrawn;
    p->mapping_state_dirty = true;
    p->state = net::States();
    p->types[0] = win_type::unknown;
    p->name = (char*)nullptr;
    p->visible_name = (char*)nullptr;
    p->icon_name = (char*)nullptr;
    p->visible_icon_name = (char*)nullptr;
    p->desktop = p->pid = 0;
    p->handled_icons = false;
    p->user_time = -1U;
    p->startup_id = nullptr;
    p->transient_for = XCB_NONE;
    p->opacity = 0xffffffffU;
    p->window_group = XCB_NONE;
    p->icon_pixmap = XCB_PIXMAP_NONE;
    p->icon_mask = XCB_PIXMAP_NONE;
    p->allowed_actions = net::Actions();
    p->has_net_support = false;
    p->class_class = (char*)nullptr;
    p->class_name = (char*)nullptr;
    p->window_role = (char*)nullptr;
    p->client_machine = (char*)nullptr;
    p->icon_sizes = nullptr;
    p->desktop_file = nullptr;
    p->gtk_application_id = nullptr;
    p->appmenu_object_path = nullptr;
    p->appmenu_service_name = nullptr;
    p->blockCompositing = false;
    p->urgency = false;
    p->input = true;
    p->initialMappingState = net::Withdrawn;
    p->protocols = net::NoProtocol;

    // p->strut.left = p->strut.right = p->strut.top = p->strut.bottom = 0;
    // p->frame_strut.left = p->frame_strut.right = p->frame_strut.top =
    // p->frame_strut.bottom = 0;

    p->properties = properties;
    p->properties2 = properties2;

    p->icon_count = 0;

    p->role = role;

    update(p->properties, p->properties2);
}

win_info::win_info(const win_info& wininfo)
{
    p = wininfo.p;
    p->ref++;
}

win_info::~win_info()
{
    refdec_nwi(p);

    if (!p->ref) {
        delete p;
    }
}

// assignment operator

const win_info& win_info::operator=(const win_info& wininfo)
{
    if (p != wininfo.p) {
        refdec_nwi(p);

        if (!p->ref) {
            delete p;
        }
    }

    p = wininfo.p;
    p->ref++;

    return *this;
}

void win_info::setIcon(net::icon icon, bool replace)
{
    setIconInternal(p->icons, p->icon_count, p->atom(_NET_WM_ICON), icon, replace);
}

void win_info::setIconInternal(net::rarray<net::icon>& icons,
                               int& icon_count,
                               xcb_atom_t property,
                               net::icon icon,
                               bool replace)
{
    assert(p->role == Client);

    if (replace) {
        for (int i = 0; i < icons.size(); i++) {
            delete[] icons[i].data;

            icons[i].data = nullptr;
            icons[i].size.width = 0;
            icons[i].size.height = 0;
        }

        icon_count = 0;
    }

    // assign icon
    icons[icon_count] = icon;
    icon_count++;

    // do a deep copy, we want to own the data
    net::icon& ni = icons[icon_count - 1];
    int sz = ni.size.width * ni.size.height;
    uint32_t* d = new uint32_t[sz];
    ni.data = (unsigned char*)d;
    memcpy(d, icon.data, sz * sizeof(uint32_t));

    // compute property length
    int proplen = 0;
    for (int i = 0; i < icon_count; i++) {
        proplen += 2 + (icons[i].size.width * icons[i].size.height);
    }

    uint32_t* prop = new uint32_t[proplen];
    uint32_t* pprop = prop;
    for (int i = 0; i < icon_count; i++) {
        // copy size into property
        *pprop++ = icons[i].size.width;
        *pprop++ = icons[i].size.height;

        // copy data into property
        sz = (icons[i].size.width * icons[i].size.height);
        uint32_t* d32 = (uint32_t*)icons[i].data;
        for (int j = 0; j < sz; j++) {
            *pprop++ = *d32++;
        }
    }

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        property,
                        XCB_ATOM_CARDINAL,
                        32,
                        proplen,
                        (const void*)prop);

    delete[] prop;
    delete[] p->icon_sizes;
    p->icon_sizes = nullptr;
}

void win_info::setIconGeometry(net::rect geometry)
{
    assert(p->role == Client);

    const qreal scaleFactor = qApp->devicePixelRatio();
    geometry.pos.x *= scaleFactor;
    geometry.pos.y *= scaleFactor;
    geometry.size.width *= scaleFactor;
    geometry.size.height *= scaleFactor;

    p->icon_geom = geometry;

    if (geometry.size.width == 0) { // Empty
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_ICON_GEOMETRY));
    } else {
        uint32_t data[4];
        data[0] = geometry.pos.x;
        data[1] = geometry.pos.y;
        data[2] = geometry.size.width;
        data[3] = geometry.size.height;

        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_ICON_GEOMETRY),
                            XCB_ATOM_CARDINAL,
                            32,
                            4,
                            (const void*)data);
    }
}

void win_info::setExtendedStrut(net::extended_strut const& extended_strut)
{
    assert(p->role == Client);

    p->extended_strut = extended_strut;

    uint32_t data[12];
    data[0] = extended_strut.left_width;
    data[1] = extended_strut.right_width;
    data[2] = extended_strut.top_width;
    data[3] = extended_strut.bottom_width;
    data[4] = extended_strut.left_start;
    data[5] = extended_strut.left_end;
    data[6] = extended_strut.right_start;
    data[7] = extended_strut.right_end;
    data[8] = extended_strut.top_start;
    data[9] = extended_strut.top_end;
    data[10] = extended_strut.bottom_start;
    data[11] = extended_strut.bottom_end;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_STRUT_PARTIAL),
                        XCB_ATOM_CARDINAL,
                        32,
                        12,
                        (const void*)data);
}

void win_info::setStrut(net::strut strut)
{
    assert(p->role == Client);

    p->strut = strut;

    uint32_t data[4];
    data[0] = strut.left;
    data[1] = strut.right;
    data[2] = strut.top;
    data[3] = strut.bottom;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_STRUT),
                        XCB_ATOM_CARDINAL,
                        32,
                        4,
                        (const void*)data);
}

void win_info::setFullscreenMonitors(net::fullscreen_monitors topology)
{
    if (p->role == Client) {
        const uint32_t data[5] = {uint32_t(topology.top),
                                  uint32_t(topology.bottom),
                                  uint32_t(topology.left),
                                  uint32_t(topology.right),
                                  1};

        send_client_message(p->conn,
                            netwm_sendevent_mask,
                            p->root,
                            p->window,
                            p->atom(_NET_WM_FULLSCREEN_MONITORS),
                            data);
    } else {
        p->fullscreen_monitors = topology;

        uint32_t data[4];
        data[0] = topology.top;
        data[1] = topology.bottom;
        data[2] = topology.left;
        data[3] = topology.right;

        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_FULLSCREEN_MONITORS),
                            XCB_ATOM_CARDINAL,
                            32,
                            4,
                            (const void*)data);
    }
}

void win_info::setState(net::States state, net::States mask)
{
    if (p->mapping_state_dirty) {
        updateWMState();
    }

    // setState() needs to know the current state, so read it even if not requested
    if ((p->properties & WMState) == 0) {
        p->properties |= WMState;

        update(WMState);

        p->properties &= ~WMState;
    }

    if (p->role == Client && p->mapping_state != Withdrawn) {
        xcb_client_message_event_t event;
        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.sequence = 0;
        event.window = p->window;
        event.type = p->atom(_NET_WM_STATE);
        event.data.data32[3] = 0;
        event.data.data32[4] = 0;

        if ((mask & Modal) && ((p->state & Modal) != (state & Modal))) {
            event.data.data32[0] = (state & Modal) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_MODAL);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & Sticky) && ((p->state & Sticky) != (state & Sticky))) {
            event.data.data32[0] = (state & Sticky) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_STICKY);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & Max) && (((p->state & mask) & Max) != (state & Max))) {
            net::States wishstate = (p->state & ~mask) | (state & mask);
            if (((wishstate & MaxHoriz) != (p->state & MaxHoriz))
                && ((wishstate & MaxVert) != (p->state & MaxVert))) {
                if ((wishstate & Max) == Max) {
                    event.data.data32[0] = 1;
                    event.data.data32[1] = p->atom(_NET_WM_STATE_MAXIMIZED_HORZ);
                    event.data.data32[2] = p->atom(_NET_WM_STATE_MAXIMIZED_VERT);
                    xcb_send_event(
                        p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
                } else if ((wishstate & Max) == 0) {
                    event.data.data32[0] = 0;
                    event.data.data32[1] = p->atom(_NET_WM_STATE_MAXIMIZED_HORZ);
                    event.data.data32[2] = p->atom(_NET_WM_STATE_MAXIMIZED_VERT);
                    xcb_send_event(
                        p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
                } else {
                    event.data.data32[0] = (wishstate & MaxHoriz) ? 1 : 0;
                    event.data.data32[1] = p->atom(_NET_WM_STATE_MAXIMIZED_HORZ);
                    event.data.data32[2] = 0;
                    xcb_send_event(
                        p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);

                    event.data.data32[0] = (wishstate & MaxVert) ? 1 : 0;
                    event.data.data32[1] = p->atom(_NET_WM_STATE_MAXIMIZED_VERT);
                    event.data.data32[2] = 0;
                    xcb_send_event(
                        p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
                }
            } else if ((wishstate & MaxVert) != (p->state & MaxVert)) {
                event.data.data32[0] = (wishstate & MaxVert) ? 1 : 0;
                event.data.data32[1] = p->atom(_NET_WM_STATE_MAXIMIZED_VERT);
                event.data.data32[2] = 0;

                xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
            } else if ((wishstate & MaxHoriz) != (p->state & MaxHoriz)) {
                event.data.data32[0] = (wishstate & MaxHoriz) ? 1 : 0;
                event.data.data32[1] = p->atom(_NET_WM_STATE_MAXIMIZED_HORZ);
                event.data.data32[2] = 0;

                xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
            }
        }

        if ((mask & Shaded) && ((p->state & Shaded) != (state & Shaded))) {
            event.data.data32[0] = (state & Shaded) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_SHADED);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & SkipTaskbar) && ((p->state & SkipTaskbar) != (state & SkipTaskbar))) {
            event.data.data32[0] = (state & SkipTaskbar) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_SKIP_TASKBAR);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & SkipPager) && ((p->state & SkipPager) != (state & SkipPager))) {
            event.data.data32[0] = (state & SkipPager) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_SKIP_PAGER);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & SkipSwitcher) && ((p->state & SkipSwitcher) != (state & SkipSwitcher))) {
            event.data.data32[0] = (state & SkipSwitcher) ? 1 : 0;
            event.data.data32[1] = p->atom(_KDE_NET_WM_STATE_SKIP_SWITCHER);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & Hidden) && ((p->state & Hidden) != (state & Hidden))) {
            event.data.data32[0] = (state & Hidden) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_HIDDEN);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & FullScreen) && ((p->state & FullScreen) != (state & FullScreen))) {
            event.data.data32[0] = (state & FullScreen) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_FULLSCREEN);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & KeepAbove) && ((p->state & KeepAbove) != (state & KeepAbove))) {
            event.data.data32[0] = (state & KeepAbove) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_ABOVE);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);

            // deprecated variant
            event.data.data32[0] = (state & KeepAbove) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_STAYS_ON_TOP);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & KeepBelow) && ((p->state & KeepBelow) != (state & KeepBelow))) {
            event.data.data32[0] = (state & KeepBelow) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_BELOW);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        if ((mask & DemandsAttention)
            && ((p->state & DemandsAttention) != (state & DemandsAttention))) {
            event.data.data32[0] = (state & DemandsAttention) ? 1 : 0;
            event.data.data32[1] = p->atom(_NET_WM_STATE_DEMANDS_ATTENTION);
            event.data.data32[2] = 0l;

            xcb_send_event(p->conn, false, p->root, netwm_sendevent_mask, (const char*)&event);
        }

        // Focused is not added here as it is effectively "read only" set by the WM, a client
        // setting it would be silly
    } else {
        p->state &= ~mask;
        p->state |= state;

        uint32_t data[50];
        int count = 0;

        // Hints
        if (p->state & Modal) {
            data[count++] = p->atom(_NET_WM_STATE_MODAL);
        }
        if (p->state & MaxVert) {
            data[count++] = p->atom(_NET_WM_STATE_MAXIMIZED_VERT);
        }
        if (p->state & MaxHoriz) {
            data[count++] = p->atom(_NET_WM_STATE_MAXIMIZED_HORZ);
        }
        if (p->state & Shaded) {
            data[count++] = p->atom(_NET_WM_STATE_SHADED);
        }
        if (p->state & Hidden) {
            data[count++] = p->atom(_NET_WM_STATE_HIDDEN);
        }
        if (p->state & FullScreen) {
            data[count++] = p->atom(_NET_WM_STATE_FULLSCREEN);
        }
        if (p->state & DemandsAttention) {
            data[count++] = p->atom(_NET_WM_STATE_DEMANDS_ATTENTION);
        }
        if (p->state & Focused) {
            data[count++] = p->atom(_NET_WM_STATE_FOCUSED);
        }

        // Policy
        if (p->state & KeepAbove) {
            data[count++] = p->atom(_NET_WM_STATE_ABOVE);
            // deprecated variant
            data[count++] = p->atom(_NET_WM_STATE_STAYS_ON_TOP);
        }
        if (p->state & KeepBelow) {
            data[count++] = p->atom(_NET_WM_STATE_BELOW);
        }
        if (p->state & Sticky) {
            data[count++] = p->atom(_NET_WM_STATE_STICKY);
        }
        if (p->state & SkipTaskbar) {
            data[count++] = p->atom(_NET_WM_STATE_SKIP_TASKBAR);
        }
        if (p->state & SkipPager) {
            data[count++] = p->atom(_NET_WM_STATE_SKIP_PAGER);
        }
        if (p->state & SkipSwitcher) {
            data[count++] = p->atom(_KDE_NET_WM_STATE_SKIP_SWITCHER);
        }

        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_STATE),
                            XCB_ATOM_ATOM,
                            32,
                            count,
                            (const void*)data);
    }
}

void win_info::setWindowType(win::win_type type)
{
    assert(p->role == Client);

    int len;
    uint32_t data[2];

    switch (type) {
    case win_type::override:
        // spec extension: override window type.  we must comply with the spec
        // and provide a fall back (normal seems best)
        data[0] = p->atom(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_NORMAL);
        len = 2;
        break;

    case win_type::dialog:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_DIALOG);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::menu:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_MENU);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::top_menu:
        // spec extension: override window type.  we must comply with the spec
        // and provide a fall back (dock seems best)
        data[0] = p->atom(_KDE_NET_WM_WINDOW_TYPE_TOPMENU);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_DOCK);
        len = 2;
        break;

    case win_type::toolbar:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_TOOLBAR);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::dock:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_DOCK);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::desktop:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_DESKTOP);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::utility:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_UTILITY);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_DIALOG); // fallback for old netwm version
        len = 2;
        break;

    case win_type::splash:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_SPLASH);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_DOCK); // fallback (dock seems best)
        len = 2;
        break;

    case win_type::dropdown_menu:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_DROPDOWN_MENU);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_MENU); // fallback (tearoff seems to be the best)
        len = 1;
        break;

    case win_type::popup_menu:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_POPUP_MENU);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_MENU); // fallback (tearoff seems to be the best)
        len = 1;
        break;

    case win_type::tooltip:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_TOOLTIP);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::notification:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_NOTIFICATION);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_UTILITY); // fallback (utility seems to be the best)
        len = 1;
        break;

    case win_type::combo_box:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_COMBO);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::dnd_icon:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_DND);
        data[1] = XCB_NONE;
        len = 1;
        break;

    case win_type::on_screen_display:
        data[0] = p->atom(_KDE_NET_WM_WINDOW_TYPE_ON_SCREEN_DISPLAY);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_NOTIFICATION);
        len = 2;
        break;

    case win_type::critical_notification:
        data[0] = p->atom(_KDE_NET_WM_WINDOW_TYPE_CRITICAL_NOTIFICATION);
        data[1] = p->atom(_NET_WM_WINDOW_TYPE_NOTIFICATION);
        len = 2;
        break;

    case win_type::applet_popup:
        data[0] = p->atom(_KDE_NET_WM_WINDOW_TYPE_APPLET_POPUP);
        data[1] = XCB_NONE;
        len = 1;
        break;

    default:
    case win_type::normal:
        data[0] = p->atom(_NET_WM_WINDOW_TYPE_NORMAL);
        data[1] = XCB_NONE;
        len = 1;
        break;
    }

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_WINDOW_TYPE),
                        XCB_ATOM_ATOM,
                        32,
                        len,
                        (const void*)&data);
}

void win_info::setName(const char* name)
{
    assert(p->role == Client);

    delete[] p->name;
    p->name = nstrdup(name);

    if (p->name[0] != '\0') {
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_NAME),
                            p->atom(UTF8_STRING),
                            8,
                            strlen(p->name),
                            (const void*)p->name);
    } else {
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_NAME));
    }
}

void win_info::setVisibleName(const char* visibleName)
{
    assert(p->role == WindowManager);

    delete[] p->visible_name;
    p->visible_name = nstrdup(visibleName);

    if (p->visible_name[0] != '\0') {
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_VISIBLE_NAME),
                            p->atom(UTF8_STRING),
                            8,
                            strlen(p->visible_name),
                            (const void*)p->visible_name);
    } else {
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_VISIBLE_NAME));
    }
}

void win_info::setIconName(const char* iconName)
{
    assert(p->role == Client);
    if (p->role != Client) {
        return;
    }

    delete[] p->icon_name;
    p->icon_name = nstrdup(iconName);

    if (p->icon_name[0] != '\0') {
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_ICON_NAME),
                            p->atom(UTF8_STRING),
                            8,
                            strlen(p->icon_name),
                            (const void*)p->icon_name);
    } else {
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_ICON_NAME));
    }
}

void win_info::setVisibleIconName(const char* visibleIconName)
{
    assert(p->role == WindowManager);

    delete[] p->visible_icon_name;
    p->visible_icon_name = nstrdup(visibleIconName);

    if (p->visible_icon_name[0] != '\0') {
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_VISIBLE_ICON_NAME),
                            p->atom(UTF8_STRING),
                            8,
                            strlen(p->visible_icon_name),
                            (const void*)p->visible_icon_name);
    } else {
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_VISIBLE_ICON_NAME));
    }
}

void win_info::setDesktop(int desktop, bool ignore_viewport)
{
    if (p->mapping_state_dirty) {
        updateWMState();
    }

    p->desktop = desktop;

    if (desktop == 0) {
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_DESKTOP));
    } else {
        uint32_t d = (desktop == OnAllDesktops ? 0xffffffff : desktop - 1);
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_DESKTOP),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            (const void*)&d);
    }
}

void win_info::setPid(int pid)
{
    assert(p->role == Client);

    p->pid = pid;
    uint32_t d = pid;
    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_PID),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        (const void*)&d);
}

void win_info::setHandledIcons(bool handled)
{
    assert(p->role == Client);

    p->handled_icons = handled;
    uint32_t d = handled;
    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_HANDLED_ICONS),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        (const void*)&d);
}

void win_info::setStartupId(const char* id)
{
    assert(p->role == Client);

    delete[] p->startup_id;
    p->startup_id = nstrdup(id);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_STARTUP_ID),
                        p->atom(UTF8_STRING),
                        8,
                        strlen(p->startup_id),
                        (const void*)p->startup_id);
}

void win_info::setOpacity(unsigned long opacity)
{
    p->opacity = opacity;
    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_WINDOW_OPACITY),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        (const void*)&p->opacity);
}

void win_info::setOpacityF(qreal opacity)
{
    setOpacity(static_cast<unsigned long>(opacity * 0xffffffff));
}

void win_info::setAllowedActions(net::Actions actions)
{
    uint32_t data[50];
    int count = 0;

    p->allowed_actions = actions;
    if (p->allowed_actions & ActionMove) {
        data[count++] = p->atom(_NET_WM_ACTION_MOVE);
    }
    if (p->allowed_actions & ActionResize) {
        data[count++] = p->atom(_NET_WM_ACTION_RESIZE);
    }
    if (p->allowed_actions & ActionMinimize) {
        data[count++] = p->atom(_NET_WM_ACTION_MINIMIZE);
    }
    if (p->allowed_actions & ActionShade) {
        data[count++] = p->atom(_NET_WM_ACTION_SHADE);
    }
    if (p->allowed_actions & ActionStick) {
        data[count++] = p->atom(_NET_WM_ACTION_STICK);
    }
    if (p->allowed_actions & ActionMaxVert) {
        data[count++] = p->atom(_NET_WM_ACTION_MAXIMIZE_VERT);
    }
    if (p->allowed_actions & ActionMaxHoriz) {
        data[count++] = p->atom(_NET_WM_ACTION_MAXIMIZE_HORZ);
    }
    if (p->allowed_actions & ActionFullScreen) {
        data[count++] = p->atom(_NET_WM_ACTION_FULLSCREEN);
    }
    if (p->allowed_actions & ActionChangeDesktop) {
        data[count++] = p->atom(_NET_WM_ACTION_CHANGE_DESKTOP);
    }
    if (p->allowed_actions & ActionClose) {
        data[count++] = p->atom(_NET_WM_ACTION_CLOSE);
    }

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_ALLOWED_ACTIONS),
                        XCB_ATOM_ATOM,
                        32,
                        count,
                        (const void*)data);
}

void win_info::setFrameExtents(net::strut strut)
{
    p->frame_strut = strut;

    uint32_t d[4];
    d[0] = strut.left;
    d[1] = strut.right;
    d[2] = strut.top;
    d[3] = strut.bottom;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_FRAME_EXTENTS),
                        XCB_ATOM_CARDINAL,
                        32,
                        4,
                        (const void*)d);
    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_KDE_NET_WM_FRAME_STRUT),
                        XCB_ATOM_CARDINAL,
                        32,
                        4,
                        (const void*)d);
}

net::strut win_info::frameExtents() const
{
    return p->frame_strut;
}

void win_info::setFrameOverlap(net::strut strut)
{
    if (strut.left != -1 || strut.top != -1 || strut.right != -1 || strut.bottom != -1) {
        strut.left = qMax(0, strut.left);
        strut.top = qMax(0, strut.top);
        strut.right = qMax(0, strut.right);
        strut.bottom = qMax(0, strut.bottom);
    }

    p->frame_overlap = strut;

    uint32_t d[4];
    d[0] = strut.left;
    d[1] = strut.right;
    d[2] = strut.top;
    d[3] = strut.bottom;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_FRAME_OVERLAP),
                        XCB_ATOM_CARDINAL,
                        32,
                        4,
                        (const void*)d);
}

net::strut win_info::frameOverlap() const
{
    return p->frame_overlap;
}

void win_info::setGtkFrameExtents(net::strut strut)
{
    p->gtk_frame_extents = strut;

    uint32_t d[4];
    d[0] = strut.left;
    d[1] = strut.right;
    d[2] = strut.top;
    d[3] = strut.bottom;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_GTK_FRAME_EXTENTS),
                        XCB_ATOM_CARDINAL,
                        32,
                        4,
                        (const void*)d);
}

net::strut win_info::gtkFrameExtents() const
{
    return p->gtk_frame_extents;
}

void win_info::setAppMenuObjectPath(const char* name)
{
    assert(p->role == Client);

    delete[] p->appmenu_object_path;
    p->appmenu_object_path = nstrdup(name);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_KDE_NET_WM_APPMENU_OBJECT_PATH),
                        XCB_ATOM_STRING,
                        8,
                        strlen(p->appmenu_object_path),
                        (const void*)p->appmenu_object_path);
}

void win_info::setAppMenuServiceName(const char* name)
{
    assert(p->role == Client);

    delete[] p->appmenu_service_name;
    p->appmenu_service_name = nstrdup(name);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_KDE_NET_WM_APPMENU_SERVICE_NAME),
                        XCB_ATOM_STRING,
                        8,
                        strlen(p->appmenu_service_name),
                        (const void*)p->appmenu_service_name);
}

const char* win_info::appMenuObjectPath() const
{
    return p->appmenu_object_path;
}

const char* win_info::appMenuServiceName() const
{
    return p->appmenu_service_name;
}

void win_info::kdeGeometry(net::rect& frame, net::rect& window)
{
    if (p->win_geom.size.width == 0 || p->win_geom.size.height == 0) {
        const xcb_get_geometry_cookie_t geometry_cookie = xcb_get_geometry(p->conn, p->window);

        const xcb_translate_coordinates_cookie_t translate_cookie
            = xcb_translate_coordinates(p->conn, p->window, p->root, 0, 0);

        xcb_get_geometry_reply_t* geometry
            = xcb_get_geometry_reply(p->conn, geometry_cookie, nullptr);
        xcb_translate_coordinates_reply_t* translated
            = xcb_translate_coordinates_reply(p->conn, translate_cookie, nullptr);

        if (geometry && translated) {
            p->win_geom.pos.x = translated->dst_x;
            p->win_geom.pos.y = translated->dst_y;

            p->win_geom.size.width = geometry->width;
            p->win_geom.size.height = geometry->height;
        }

        if (geometry) {
            free(geometry);
        }

        if (translated) {
            free(translated);
        }
    }

    // TODO try to work also without _NET_WM_FRAME_EXTENTS
    window = p->win_geom;

    frame.pos.x = window.pos.x - p->frame_strut.left;
    frame.pos.y = window.pos.y - p->frame_strut.top;
    frame.size.width = window.size.width + p->frame_strut.left + p->frame_strut.right;
    frame.size.height = window.size.height + p->frame_strut.top + p->frame_strut.bottom;
}

net::icon win_info::icon(int width, int height) const
{
    return iconInternal(p->icons, p->icon_count, width, height);
}

const int* win_info::iconSizes() const
{
    if (p->icon_sizes == nullptr) {
        p->icon_sizes = new int[p->icon_count * 2 + 2];
        for (int i = 0; i < p->icon_count; ++i) {
            p->icon_sizes[i * 2] = p->icons[i].size.width;
            p->icon_sizes[i * 2 + 1] = p->icons[i].size.height;
        }
        p->icon_sizes[p->icon_count * 2] = 0; // terminator
        p->icon_sizes[p->icon_count * 2 + 1] = 0;
    }
    return p->icon_sizes;
}

net::icon
win_info::iconInternal(net::rarray<net::icon>& icons, int icon_count, int width, int height) const
{
    net::icon result;

    if (!icon_count) {
        result.size.width = 0;
        result.size.height = 0;
        result.data = nullptr;
        return result;
    }

    // find the largest icon
    result = icons[0];
    for (int i = 1; i < icons.size(); i++) {
        if (icons[i].size.width >= result.size.width
            && icons[i].size.height >= result.size.height) {
            result = icons[i];
        }
    }

    // return the largest icon if w and h are -1
    if (width == -1 && height == -1) {
        return result;
    }

    // find the icon that's closest in size to w x h...
    for (int i = 0; i < icons.size(); i++) {
        if ((icons[i].size.width >= width && icons[i].size.width < result.size.width)
            && (icons[i].size.height >= height && icons[i].size.height < result.size.height)) {
            result = icons[i];
        }
    }

    return result;
}

void win_info::setUserTime(xcb_timestamp_t time)
{
    assert(p->role == Client);

    p->user_time = time;
    uint32_t d = time;

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_NET_WM_USER_TIME),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        (const void*)&d);
}

net::Properties win_info::event(xcb_generic_event_t* ev)
{
    net::Properties properties;
    event(ev, &properties);
    return properties;
}

void win_info::event(xcb_generic_event_t* event,
                     net::Properties* properties,
                     net::Properties2* properties2)
{
    net::Properties dirty;
    net::Properties2 dirty2;
    bool do_update = false;
    const uint8_t eventType = event->response_type & ~0x80;

    if (eventType == XCB_CLIENT_MESSAGE
        && reinterpret_cast<xcb_client_message_event_t*>(event)->format == 32) {
        auto message = reinterpret_cast<xcb_client_message_event_t*>(event);

        if (message->type == p->atom(_NET_WM_STATE)) {
            dirty = WMState;

            // we need to generate a change mask
            int i;
            net::States state = net::States();
            net::States mask = net::States();

            for (i = 1; i < 3; i++) {
                if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_MODAL)) {
                    mask |= Modal;
                } else if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_STICKY)) {
                    mask |= Sticky;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_MAXIMIZED_VERT)) {
                    mask |= MaxVert;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_MAXIMIZED_HORZ)) {
                    mask |= MaxHoriz;
                } else if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_SHADED)) {
                    mask |= Shaded;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_SKIP_TASKBAR)) {
                    mask |= SkipTaskbar;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_SKIP_PAGER)) {
                    mask |= SkipPager;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_KDE_NET_WM_STATE_SKIP_SWITCHER)) {
                    mask |= SkipSwitcher;
                } else if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_HIDDEN)) {
                    mask |= Hidden;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_FULLSCREEN)) {
                    mask |= FullScreen;
                } else if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_ABOVE)) {
                    mask |= KeepAbove;
                } else if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_BELOW)) {
                    mask |= KeepBelow;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_DEMANDS_ATTENTION)) {
                    mask |= DemandsAttention;
                } else if ((xcb_atom_t)message->data.data32[i]
                           == p->atom(_NET_WM_STATE_STAYS_ON_TOP)) {
                    mask |= KeepAbove;
                } else if ((xcb_atom_t)message->data.data32[i] == p->atom(_NET_WM_STATE_FOCUSED)) {
                    mask |= Focused;
                }
            }

            // when removing, we just leave newstate == 0
            switch (message->data.data32[0]) {
            case 1: // set
                // to set... the change state should be the same as the mask
                state = mask;
                break;

            case 2: // toggle
                // to toggle, we need to xor the current state with the new state
                state = (p->state & mask) ^ mask;
                break;

            default:
                // to clear state, the new state should stay zero
                ;
            }

            changeState(state, mask);
        } else if (message->type == p->atom(_NET_WM_DESKTOP)) {
            dirty = WMDesktop;

            if (message->data.data32[0] == (unsigned)OnAllDesktops) {
                changeDesktop(OnAllDesktops);
            } else {
                changeDesktop(message->data.data32[0] + 1);
            }
        } else if (message->type == p->atom(_NET_WM_FULLSCREEN_MONITORS)) {
            dirty2 = WM2FullscreenMonitors;

            net::fullscreen_monitors topology;
            topology.top = message->data.data32[0];
            topology.bottom = message->data.data32[1];
            topology.left = message->data.data32[2];
            topology.right = message->data.data32[3];

            changeFullscreenMonitors(topology);
        }
    }

    if (eventType == XCB_PROPERTY_NOTIFY) {
        auto pe = reinterpret_cast<xcb_property_notify_event_t*>(event);

        if (pe->atom == p->atom(_NET_WM_NAME)) {
            dirty |= WMName;
        } else if (pe->atom == p->atom(_NET_WM_VISIBLE_NAME)) {
            dirty |= WMVisibleName;
        } else if (pe->atom == p->atom(_NET_WM_DESKTOP)) {
            dirty |= WMDesktop;
        } else if (pe->atom == p->atom(_NET_WM_WINDOW_TYPE)) {
            dirty |= WMWindowType;
        } else if (pe->atom == p->atom(_NET_WM_STATE)) {
            dirty |= WMState;
        } else if (pe->atom == p->atom(_NET_WM_STRUT)) {
            dirty |= WMStrut;
        } else if (pe->atom == p->atom(_NET_WM_STRUT_PARTIAL)) {
            dirty2 |= WM2ExtendedStrut;
        } else if (pe->atom == p->atom(_NET_WM_ICON_GEOMETRY)) {
            dirty |= WMIconGeometry;
        } else if (pe->atom == p->atom(_NET_WM_ICON)) {
            dirty |= WMIcon;
        } else if (pe->atom == p->atom(_NET_WM_PID)) {
            dirty |= WMPid;
        } else if (pe->atom == p->atom(_NET_WM_HANDLED_ICONS)) {
            dirty |= WMHandledIcons;
        } else if (pe->atom == p->atom(_NET_STARTUP_ID)) {
            dirty2 |= WM2StartupId;
        } else if (pe->atom == p->atom(_NET_WM_WINDOW_OPACITY)) {
            dirty2 |= WM2Opacity;
        } else if (pe->atom == p->atom(_NET_WM_ALLOWED_ACTIONS)) {
            dirty2 |= WM2AllowedActions;
        } else if (pe->atom == p->atom(WM_STATE)) {
            dirty |= XAWMState;
        } else if (pe->atom == p->atom(_NET_FRAME_EXTENTS)) {
            dirty |= WMFrameExtents;
        } else if (pe->atom == p->atom(_KDE_NET_WM_FRAME_STRUT)) {
            dirty |= WMFrameExtents;
        } else if (pe->atom == p->atom(_NET_WM_FRAME_OVERLAP)) {
            dirty2 |= WM2FrameOverlap;
        } else if (pe->atom == p->atom(_NET_WM_ICON_NAME)) {
            dirty |= WMIconName;
        } else if (pe->atom == p->atom(_NET_WM_VISIBLE_ICON_NAME)) {
            dirty |= WMVisibleIconName;
        } else if (pe->atom == p->atom(_NET_WM_USER_TIME)) {
            dirty2 |= WM2UserTime;
        } else if (pe->atom == XCB_ATOM_WM_HINTS) {
            dirty2 |= WM2GroupLeader;
            dirty2 |= WM2Urgency;
            dirty2 |= WM2Input;
            dirty2 |= WM2InitialMappingState;
            dirty2 |= WM2IconPixmap;
        } else if (pe->atom == XCB_ATOM_WM_TRANSIENT_FOR) {
            dirty2 |= WM2TransientFor;
        } else if (pe->atom == XCB_ATOM_WM_CLASS) {
            dirty2 |= WM2WindowClass;
        } else if (pe->atom == p->atom(WM_WINDOW_ROLE)) {
            dirty2 |= WM2WindowRole;
        } else if (pe->atom == XCB_ATOM_WM_CLIENT_MACHINE) {
            dirty2 |= WM2ClientMachine;
        } else if (pe->atom == p->atom(_KDE_NET_WM_ACTIVITIES)) {
            dirty2 |= WM2Activities;
        } else if (pe->atom == p->atom(_KDE_NET_WM_BLOCK_COMPOSITING)
                   || pe->atom == p->atom(_NET_WM_BYPASS_COMPOSITOR)) {
            dirty2 |= WM2BlockCompositing;
        } else if (pe->atom == p->atom(_KDE_NET_WM_SHADOW)) {
            dirty2 |= WM2KDEShadow;
        } else if (pe->atom == p->atom(WM_PROTOCOLS)) {
            dirty2 |= WM2Protocols;
        } else if (pe->atom == p->atom(_NET_WM_OPAQUE_REGION)) {
            dirty2 |= WM2OpaqueRegion;
        } else if (pe->atom == p->atom(_KDE_NET_WM_DESKTOP_FILE)) {
            dirty2 = WM2DesktopFileName;
        } else if (pe->atom == p->atom(_GTK_APPLICATION_ID)) {
            dirty2 = WM2GTKApplicationId;
        } else if (pe->atom == p->atom(_NET_WM_FULLSCREEN_MONITORS)) {
            dirty2 = WM2FullscreenMonitors;
        } else if (pe->atom == p->atom(_GTK_FRAME_EXTENTS)) {
            dirty2 |= WM2GTKFrameExtents;
        } else if (pe->atom == p->atom(_GTK_SHOW_WINDOW_MENU)) {
            dirty2 |= WM2GTKShowWindowMenu;
        } else if (pe->atom == p->atom(_KDE_NET_WM_APPMENU_SERVICE_NAME)) {
            dirty2 |= WM2AppMenuServiceName;
        } else if (pe->atom == p->atom(_KDE_NET_WM_APPMENU_OBJECT_PATH)) {
            dirty2 |= WM2AppMenuObjectPath;
        }

        do_update = true;
    } else if (eventType == XCB_CONFIGURE_NOTIFY) {
        dirty |= WMGeometry;

        // update window geometry
        auto configure = reinterpret_cast<xcb_configure_notify_event_t*>(event);
        p->win_geom.pos.x = configure->x;
        p->win_geom.pos.y = configure->y;
        p->win_geom.size.width = configure->width;
        p->win_geom.size.height = configure->height;
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

void win_info::updateWMState()
{
    update(XAWMState);
}

void win_info::update(net::Properties dirtyProperties, net::Properties2 dirtyProperties2)
{
    Properties dirty = dirtyProperties & p->properties;
    Properties2 dirty2 = dirtyProperties2 & p->properties2;

    // We *always* want to update WM_STATE if set in dirty_props
    if (dirtyProperties & XAWMState) {
        dirty |= XAWMState;
    }

    xcb_get_property_cookie_t cookies[255];
    int c = 0;

    if (dirty & XAWMState) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(WM_STATE), p->atom(WM_STATE), 0, 1);
    }

    if (dirty & WMState) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_STATE), XCB_ATOM_ATOM, 0, 2048);
    }

    if (dirty & WMDesktop) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_DESKTOP), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty & WMName) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_WM_NAME),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & WMVisibleName) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_WM_VISIBLE_NAME),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & WMIconName) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_WM_ICON_NAME),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & WMVisibleIconName) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_WM_VISIBLE_ICON_NAME),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty & WMWindowType) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_WINDOW_TYPE), XCB_ATOM_ATOM, 0, 2048);
    }

    if (dirty & WMStrut) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_STRUT), XCB_ATOM_CARDINAL, 0, 4);
    }

    if (dirty2 & WM2ExtendedStrut) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_STRUT_PARTIAL), XCB_ATOM_CARDINAL, 0, 12);
    }

    if (dirty2 & WM2FullscreenMonitors) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_WM_FULLSCREEN_MONITORS),
                                        XCB_ATOM_CARDINAL,
                                        0,
                                        4);
    }

    if (dirty & WMIconGeometry) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_ICON_GEOMETRY), XCB_ATOM_CARDINAL, 0, 4);
    }

    if (dirty & WMIcon) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_ICON), XCB_ATOM_CARDINAL, 0, 0xffffffff);
    }

    if (dirty & WMFrameExtents) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_FRAME_EXTENTS), XCB_ATOM_CARDINAL, 0, 4);
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_KDE_NET_WM_FRAME_STRUT), XCB_ATOM_CARDINAL, 0, 4);
    }

    if (dirty2 & WM2FrameOverlap) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_FRAME_OVERLAP), XCB_ATOM_CARDINAL, 0, 4);
    }

    if (dirty2 & WM2Activities) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_KDE_NET_WM_ACTIVITIES),
                                        XCB_ATOM_STRING,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2BlockCompositing) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_KDE_NET_WM_BLOCK_COMPOSITING),
                                        XCB_ATOM_CARDINAL,
                                        0,
                                        1);
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_BYPASS_COMPOSITOR), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty & WMPid) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_PID), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty2 & WM2StartupId) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_STARTUP_ID),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2Opacity) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_WINDOW_OPACITY), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty2 & WM2AllowedActions) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_ALLOWED_ACTIONS), XCB_ATOM_ATOM, 0, 2048);
    }

    if (dirty2 & WM2UserTime) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_NET_WM_USER_TIME), XCB_ATOM_CARDINAL, 0, 1);
    }

    if (dirty2 & WM2TransientFor) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 0, 1);
    }

    if (dirty2
        & (WM2GroupLeader | WM2Urgency | WM2Input | WM2InitialMappingState | WM2IconPixmap)) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 0, 9);
    }

    if (dirty2 & WM2WindowClass) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, MAX_PROP_SIZE);
    }

    if (dirty2 & WM2WindowRole) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(WM_WINDOW_ROLE), XCB_ATOM_STRING, 0, MAX_PROP_SIZE);
    }

    if (dirty2 & WM2ClientMachine) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        XCB_ATOM_WM_CLIENT_MACHINE,
                                        XCB_ATOM_STRING,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2Protocols) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(WM_PROTOCOLS), XCB_ATOM_ATOM, 0, 2048);
    }

    if (dirty2 & WM2OpaqueRegion) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_NET_WM_OPAQUE_REGION),
                                        XCB_ATOM_CARDINAL,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2DesktopFileName) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_KDE_NET_WM_DESKTOP_FILE),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2GTKApplicationId) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_GTK_APPLICATION_ID),
                                        p->atom(UTF8_STRING),
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2GTKFrameExtents) {
        cookies[c++] = xcb_get_property(
            p->conn, false, p->window, p->atom(_GTK_FRAME_EXTENTS), XCB_ATOM_CARDINAL, 0, 4);
    }

    if (dirty2 & WM2AppMenuObjectPath) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_KDE_NET_WM_APPMENU_OBJECT_PATH),
                                        XCB_ATOM_STRING,
                                        0,
                                        MAX_PROP_SIZE);
    }

    if (dirty2 & WM2AppMenuServiceName) {
        cookies[c++] = xcb_get_property(p->conn,
                                        false,
                                        p->window,
                                        p->atom(_KDE_NET_WM_APPMENU_SERVICE_NAME),
                                        XCB_ATOM_STRING,
                                        0,
                                        MAX_PROP_SIZE);
    }

    c = 0;

    if (dirty & XAWMState) {
        p->mapping_state = Withdrawn;

        bool success;
        uint32_t state
            = get_value_reply<uint32_t>(p->conn, cookies[c++], p->atom(WM_STATE), 0, &success);

        if (success) {
            switch (state) {
            case 3: // IconicState
                p->mapping_state = Iconic;
                break;

            case 1: // NormalState
                p->mapping_state = Visible;
                break;

            case 0: // WithdrawnState
            default:
                p->mapping_state = Withdrawn;
                break;
            }

            p->mapping_state_dirty = false;
        }
    }

    if (dirty & WMState) {
        p->state = net::States();
        auto const states = get_array_reply<xcb_atom_t>(p->conn, cookies[c++], XCB_ATOM_ATOM);

        for (auto const state : states) {
            if (state == p->atom(_NET_WM_STATE_MODAL)) {
                p->state |= Modal;
            }

            else if (state == p->atom(_NET_WM_STATE_STICKY)) {
                p->state |= Sticky;
            }

            else if (state == p->atom(_NET_WM_STATE_MAXIMIZED_VERT)) {
                p->state |= MaxVert;
            }

            else if (state == p->atom(_NET_WM_STATE_MAXIMIZED_HORZ)) {
                p->state |= MaxHoriz;
            }

            else if (state == p->atom(_NET_WM_STATE_SHADED)) {
                p->state |= Shaded;
            }

            else if (state == p->atom(_NET_WM_STATE_SKIP_TASKBAR)) {
                p->state |= SkipTaskbar;
            }

            else if (state == p->atom(_NET_WM_STATE_SKIP_PAGER)) {
                p->state |= SkipPager;
            }

            else if (state == p->atom(_KDE_NET_WM_STATE_SKIP_SWITCHER)) {
                p->state |= SkipSwitcher;
            }

            else if (state == p->atom(_NET_WM_STATE_HIDDEN)) {
                p->state |= Hidden;
            }

            else if (state == p->atom(_NET_WM_STATE_FULLSCREEN)) {
                p->state |= FullScreen;
            }

            else if (state == p->atom(_NET_WM_STATE_ABOVE)) {
                p->state |= KeepAbove;
            }

            else if (state == p->atom(_NET_WM_STATE_BELOW)) {
                p->state |= KeepBelow;
            }

            else if (state == p->atom(_NET_WM_STATE_DEMANDS_ATTENTION)) {
                p->state |= DemandsAttention;
            }

            else if (state == p->atom(_NET_WM_STATE_STAYS_ON_TOP)) {
                p->state |= KeepAbove;
            }

            else if (state == p->atom(_NET_WM_STATE_FOCUSED)) {
                p->state |= Focused;
            }
        }
    }

    if (dirty & WMDesktop) {
        p->desktop = 0;

        bool success;
        uint32_t desktop
            = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0, &success);

        if (success) {
            if (desktop != 0xffffffff) {
                p->desktop = desktop + 1;
            } else {
                p->desktop = OnAllDesktops;
            }
        }
    }

    if (dirty & WMName) {
        delete[] p->name;
        p->name = nullptr;

        const QByteArray str = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (str.length() > 0) {
            p->name = nstrndup(str.constData(), str.length());
        }
    }

    if (dirty & WMVisibleName) {
        delete[] p->visible_name;
        p->visible_name = nullptr;

        const QByteArray str = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (str.length() > 0) {
            p->visible_name = nstrndup(str.constData(), str.length());
        }
    }

    if (dirty & WMIconName) {
        delete[] p->icon_name;
        p->icon_name = nullptr;

        const QByteArray str = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (str.length() > 0) {
            p->icon_name = nstrndup(str.constData(), str.length());
        }
    }

    if (dirty & WMVisibleIconName) {
        delete[] p->visible_icon_name;
        p->visible_icon_name = nullptr;

        const QByteArray str = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (str.length() > 0) {
            p->visible_icon_name = nstrndup(str.constData(), str.length());
        }
    }

    if (dirty & WMWindowType) {
        p->types.reset();
        p->types[0] = win_type::unknown;
        p->has_net_support = false;

        const QVector<xcb_atom_t> types
            = get_array_reply<xcb_atom_t>(p->conn, cookies[c++], XCB_ATOM_ATOM);

        if (!types.isEmpty()) {
            p->has_net_support = true;
            int pos = 0;

            for (const xcb_atom_t type : types) {
                if (type == p->atom(_NET_WM_WINDOW_TYPE_NORMAL)) {
                    p->types[pos++] = win_type::normal;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_DESKTOP)) {
                    p->types[pos++] = win_type::desktop;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_DOCK)) {
                    p->types[pos++] = win_type::dock;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_TOOLBAR)) {
                    p->types[pos++] = win_type::toolbar;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_MENU)) {
                    p->types[pos++] = win_type::menu;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_DIALOG)) {
                    p->types[pos++] = win_type::dialog;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_UTILITY)) {
                    p->types[pos++] = win_type::utility;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_SPLASH)) {
                    p->types[pos++] = win_type::splash;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_DROPDOWN_MENU)) {
                    p->types[pos++] = win_type::dropdown_menu;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_POPUP_MENU)) {
                    p->types[pos++] = win_type::popup_menu;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_TOOLTIP)) {
                    p->types[pos++] = win_type::tooltip;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_NOTIFICATION)) {
                    p->types[pos++] = win_type::notification;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_COMBO)) {
                    p->types[pos++] = win_type::combo_box;
                }

                else if (type == p->atom(_NET_WM_WINDOW_TYPE_DND)) {
                    p->types[pos++] = win_type::dnd_icon;
                }

                else if (type == p->atom(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE)) {
                    p->types[pos++] = win_type::override;
                }

                else if (type == p->atom(_KDE_NET_WM_WINDOW_TYPE_TOPMENU)) {
                    p->types[pos++] = win_type::top_menu;
                }

                else if (type == p->atom(_KDE_NET_WM_WINDOW_TYPE_ON_SCREEN_DISPLAY)) {
                    p->types[pos++] = win_type::on_screen_display;
                }

                else if (type == p->atom(_KDE_NET_WM_WINDOW_TYPE_CRITICAL_NOTIFICATION)) {
                    p->types[pos++] = win_type::critical_notification;
                }

                else if (type == p->atom(_KDE_NET_WM_WINDOW_TYPE_APPLET_POPUP)) {
                    p->types[pos++] = win_type::applet_popup;
                }
            }
        }
    }

    if (dirty & WMStrut) {
        p->strut = net::strut();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 4) {
            p->strut.left = data[0];
            p->strut.right = data[1];
            p->strut.top = data[2];
            p->strut.bottom = data[3];
        }
    }

    if (dirty2 & WM2ExtendedStrut) {
        p->extended_strut = net::extended_strut();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 12) {
            p->extended_strut.left_width = data[0];
            p->extended_strut.right_width = data[1];
            p->extended_strut.top_width = data[2];
            p->extended_strut.bottom_width = data[3];
            p->extended_strut.left_start = data[4];
            p->extended_strut.left_end = data[5];
            p->extended_strut.right_start = data[6];
            p->extended_strut.right_end = data[7];
            p->extended_strut.top_start = data[8];
            p->extended_strut.top_end = data[9];
            p->extended_strut.bottom_start = data[10];
            p->extended_strut.bottom_end = data[11];
        }
    }

    if (dirty2 & WM2FullscreenMonitors) {
        p->fullscreen_monitors = net::fullscreen_monitors();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 4) {
            p->fullscreen_monitors.top = data[0];
            p->fullscreen_monitors.bottom = data[1];
            p->fullscreen_monitors.left = data[2];
            p->fullscreen_monitors.right = data[3];
        }
    }

    if (dirty & WMIconGeometry) {
        p->icon_geom = net::rect();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 4) {
            p->icon_geom.pos.x = data[0];
            p->icon_geom.pos.y = data[1];
            p->icon_geom.size.width = data[2];
            p->icon_geom.size.height = data[3];
        }
    }

    if (dirty & WMIcon) {
        readIcon(p->conn, cookies[c++], p->icons, p->icon_count);
        delete[] p->icon_sizes;
        p->icon_sizes = nullptr;
    }

    if (dirty & WMFrameExtents) {
        p->frame_strut = net::strut();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);

        if (data.isEmpty()) {
            data = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        } else {
            xcb_discard_reply(p->conn, cookies[c++].sequence);
        }

        if (data.count() == 4) {
            p->frame_strut.left = data[0];
            p->frame_strut.right = data[1];
            p->frame_strut.top = data[2];
            p->frame_strut.bottom = data[3];
        }
    }

    if (dirty2 & WM2FrameOverlap) {
        p->frame_overlap = net::strut();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 4) {
            p->frame_overlap.left = data[0];
            p->frame_overlap.right = data[1];
            p->frame_overlap.top = data[2];
            p->frame_overlap.bottom = data[3];
        }
    }

    if (dirty2 & WM2BlockCompositing) {
        bool success;
        p->blockCompositing = false;

        // _KDE_NET_WM_BLOCK_COMPOSITING
        uint32_t data
            = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0, &success);
        if (success) {
            p->blockCompositing = bool(data);
        }

        // _NET_WM_BYPASS_COMPOSITOR
        data = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0, &success);
        if (success) {
            switch (data) {
            case 1:
                p->blockCompositing = true;
                break;
            case 2:
                p->blockCompositing = false;
                break;
            default:
                break; // yes, the standard /is/ that stupid.
            }
        }
    }

    if (dirty & WMPid) {
        p->pid = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0);
    }

    if (dirty2 & WM2StartupId) {
        delete[] p->startup_id;
        p->startup_id = nullptr;

        const QByteArray id = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (id.length() > 0) {
            p->startup_id = nstrndup(id.constData(), id.length());
        }
    }

    if (dirty2 & WM2Opacity) {
        p->opacity
            = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0xffffffff);
    }

    if (dirty2 & WM2AllowedActions) {
        p->allowed_actions = net::Actions();

        const QVector<xcb_atom_t> actions
            = get_array_reply<xcb_atom_t>(p->conn, cookies[c++], XCB_ATOM_ATOM);
        if (!actions.isEmpty()) {
            for (const xcb_atom_t action : actions) {
                if (action == p->atom(_NET_WM_ACTION_MOVE)) {
                    p->allowed_actions |= ActionMove;
                }

                else if (action == p->atom(_NET_WM_ACTION_RESIZE)) {
                    p->allowed_actions |= ActionResize;
                }

                else if (action == p->atom(_NET_WM_ACTION_MINIMIZE)) {
                    p->allowed_actions |= ActionMinimize;
                }

                else if (action == p->atom(_NET_WM_ACTION_SHADE)) {
                    p->allowed_actions |= ActionShade;
                }

                else if (action == p->atom(_NET_WM_ACTION_STICK)) {
                    p->allowed_actions |= ActionStick;
                }

                else if (action == p->atom(_NET_WM_ACTION_MAXIMIZE_VERT)) {
                    p->allowed_actions |= ActionMaxVert;
                }

                else if (action == p->atom(_NET_WM_ACTION_MAXIMIZE_HORZ)) {
                    p->allowed_actions |= ActionMaxHoriz;
                }

                else if (action == p->atom(_NET_WM_ACTION_FULLSCREEN)) {
                    p->allowed_actions |= ActionFullScreen;
                }

                else if (action == p->atom(_NET_WM_ACTION_CHANGE_DESKTOP)) {
                    p->allowed_actions |= ActionChangeDesktop;
                }

                else if (action == p->atom(_NET_WM_ACTION_CLOSE)) {
                    p->allowed_actions |= ActionClose;
                }
            }
        }
    }

    if (dirty2 & WM2UserTime) {
        p->user_time = -1U;

        bool success;
        uint32_t value
            = get_value_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL, 0, &success);

        if (success) {
            p->user_time = value;
        }
    }

    if (dirty2 & WM2TransientFor) {
        p->transient_for = get_value_reply<xcb_window_t>(p->conn, cookies[c++], XCB_ATOM_WINDOW, 0);
    }

    if (dirty2
        & (WM2GroupLeader | WM2Urgency | WM2Input | WM2InitialMappingState | WM2IconPixmap)) {
        xcb_get_property_reply_t* reply = xcb_get_property_reply(p->conn, cookies[c++], nullptr);

        if (reply && reply->format == 32 && reply->value_len == 9
            && reply->type == XCB_ATOM_WM_HINTS) {
            kde_wm_hints* hints = reinterpret_cast<kde_wm_hints*>(xcb_get_property_value(reply));

            if (hints->flags & (1 << 0) /*Input*/) {
                p->input = hints->input;
            }
            if (hints->flags & (1 << 1) /*StateHint*/) {
                switch (hints->initial_state) {
                case 3: // IconicState
                    p->initialMappingState = Iconic;
                    break;

                case 1: // NormalState
                    p->initialMappingState = Visible;
                    break;

                case 0: // WithdrawnState
                default:
                    p->initialMappingState = Withdrawn;
                    break;
                }
            }
            if (hints->flags & (1 << 2) /*IconPixmapHint*/) {
                p->icon_pixmap = hints->icon_pixmap;
            }
            if (hints->flags & (1 << 5) /*IconMaskHint*/) {
                p->icon_mask = hints->icon_mask;
            }
            if (hints->flags & (1 << 6) /*WindowGroupHint*/) {
                p->window_group = hints->window_group;
            }
            p->urgency = (hints->flags & (1 << 8) /*UrgencyHint*/);
        }

        if (reply) {
            free(reply);
        }
    }

    if (dirty2 & WM2WindowClass) {
        delete[] p->class_name;
        delete[] p->class_class;
        p->class_name = nullptr;
        p->class_class = nullptr;

        const QList<QByteArray> list = get_stringlist_reply(p->conn, cookies[c++], XCB_ATOM_STRING);
        if (list.count() == 2) {
            p->class_name = nstrdup(list.at(0).constData());
            p->class_class = nstrdup(list.at(1).constData());
        } else if (list.count() == 1) { // Not fully compliant client. Provides a single string
            p->class_name = nstrdup(list.at(0).constData());
            p->class_class = nstrdup(list.at(0).constData());
        }
    }

    if (dirty2 & WM2WindowRole) {
        delete[] p->window_role;
        p->window_role = nullptr;

        const QByteArray role = get_string_reply(p->conn, cookies[c++], XCB_ATOM_STRING);
        if (role.length() > 0) {
            p->window_role = nstrndup(role.constData(), role.length());
        }
    }

    if (dirty2 & WM2ClientMachine) {
        delete[] p->client_machine;
        p->client_machine = nullptr;

        const QByteArray value = get_string_reply(p->conn, cookies[c++], XCB_ATOM_STRING);
        if (value.length() > 0) {
            p->client_machine = nstrndup(value.constData(), value.length());
        }
    }

    if (dirty2 & WM2Protocols) {
        const QVector<xcb_atom_t> protocols
            = get_array_reply<xcb_atom_t>(p->conn, cookies[c++], XCB_ATOM_ATOM);
        p->protocols = net::NoProtocol;
        for (auto it = protocols.begin(); it != protocols.end(); ++it) {
            if ((*it) == p->atom(WM_TAKE_FOCUS)) {
                p->protocols |= TakeFocusProtocol;
            } else if ((*it) == p->atom(WM_DELETE_WINDOW)) {
                p->protocols |= DeleteWindowProtocol;
            } else if ((*it) == p->atom(_NET_WM_PING)) {
                p->protocols |= PingProtocol;
            } else if ((*it) == p->atom(_NET_WM_SYNC_REQUEST)) {
                p->protocols |= SyncRequestProtocol;
            } else if ((*it) == p->atom(_NET_WM_CONTEXT_HELP)) {
                p->protocols |= ContextHelpProtocol;
            }
        }
    }

    if (dirty2 & WM2OpaqueRegion) {
        const QVector<qint32> values
            = get_array_reply<qint32>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        p->opaqueRegion.clear();
        p->opaqueRegion.reserve(values.count() / 4);
        for (int i = 0; i < values.count() - 3; i += 4) {
            net::rect rect;
            rect.pos.x = values.at(i);
            rect.pos.y = values.at(i + 1);
            rect.size.width = values.at(i + 2);
            rect.size.height = values.at(i + 3);
            p->opaqueRegion.push_back(rect);
        }
    }

    if (dirty2 & WM2DesktopFileName) {
        delete[] p->desktop_file;
        p->desktop_file = nullptr;

        const QByteArray id = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (id.length() > 0) {
            p->desktop_file = nstrndup(id.constData(), id.length());
        }
    }

    if (dirty2 & WM2GTKApplicationId) {
        delete[] p->gtk_application_id;
        p->gtk_application_id = nullptr;

        const QByteArray id = get_string_reply(p->conn, cookies[c++], p->atom(UTF8_STRING));
        if (id.length() > 0) {
            p->gtk_application_id = nstrndup(id.constData(), id.length());
        }
    }

    if (dirty2 & WM2GTKFrameExtents) {
        p->gtk_frame_extents = net::strut();

        QVector<uint32_t> data
            = get_array_reply<uint32_t>(p->conn, cookies[c++], XCB_ATOM_CARDINAL);
        if (data.count() == 4) {
            p->gtk_frame_extents.left = data[0];
            p->gtk_frame_extents.right = data[1];
            p->gtk_frame_extents.top = data[2];
            p->gtk_frame_extents.bottom = data[3];
        }
    }

    if (dirty2 & WM2AppMenuObjectPath) {
        delete[] p->appmenu_object_path;
        p->appmenu_object_path = nullptr;

        const QByteArray id = get_string_reply(p->conn, cookies[c++], XCB_ATOM_STRING);
        if (id.length() > 0) {
            p->appmenu_object_path = nstrndup(id.constData(), id.length());
        }
    }

    if (dirty2 & WM2AppMenuServiceName) {
        delete[] p->appmenu_service_name;
        p->appmenu_service_name = nullptr;

        const QByteArray id = get_string_reply(p->conn, cookies[c++], XCB_ATOM_STRING);
        if (id.length() > 0) {
            p->appmenu_service_name = nstrndup(id.constData(), id.length());
        }
    }
}

net::rect win_info::iconGeometry() const
{
    return p->icon_geom;
}

net::States win_info::state() const
{
    return p->state;
}

net::strut win_info::strut() const
{
    return p->strut;
}

net::extended_strut win_info::extendedStrut() const
{
    return p->extended_strut;
}

net::fullscreen_monitors win_info::fullscreenMonitors() const
{
    return p->fullscreen_monitors;
}

bool typeMatchesMask(win::win_type type, win::window_type_mask mask)
{
    switch (type) {
        // clang-format off
#define CHECK_TYPE_MASK( type ) \
case win::win_type::type: \
    if( flags(mask & win::window_type_mask::type) ) \
        return true; \
    break;
        // clang-format on
        CHECK_TYPE_MASK(normal)
        CHECK_TYPE_MASK(desktop)
        CHECK_TYPE_MASK(dock)
        CHECK_TYPE_MASK(toolbar)
        CHECK_TYPE_MASK(menu)
        CHECK_TYPE_MASK(dialog)
        CHECK_TYPE_MASK(override)
        CHECK_TYPE_MASK(top_menu)
        CHECK_TYPE_MASK(utility)
        CHECK_TYPE_MASK(splash)
        CHECK_TYPE_MASK(dropdown_menu)
        CHECK_TYPE_MASK(popup_menu)
        CHECK_TYPE_MASK(tooltip)
        CHECK_TYPE_MASK(notification)
        CHECK_TYPE_MASK(combo_box)
        CHECK_TYPE_MASK(dnd_icon)
        CHECK_TYPE_MASK(on_screen_display)
        CHECK_TYPE_MASK(critical_notification)
        CHECK_TYPE_MASK(applet_popup)
#undef CHECK_TYPE_MASK
    default:
        break;
    }
    return false;
}

win::win_type win_info::windowType(win::window_type_mask supported_types) const
{
    for (int i = 0; i < p->types.size(); ++i) {
        // return the type only if the application supports it
        if (typeMatchesMask(p->types[i], supported_types)) {
            return p->types[i];
        }
    }
    return win::win_type::unknown;
}

bool win_info::hasWindowType() const
{
    return p->types.size() > 0;
}

const char* win_info::name() const
{
    return p->name;
}

const char* win_info::visibleName() const
{
    return p->visible_name;
}

const char* win_info::iconName() const
{
    return p->icon_name;
}

const char* win_info::visibleIconName() const
{
    return p->visible_icon_name;
}

int win_info::desktop(/*bool ignore_viewport*/) const
{
    // TODO(romangg): Do we still need this?
    /*
    if (!ignore_viewport && KX11Extras::mapViewport()) {
        const KWindowInfo info(p->window, net::WMDesktop);
        return info.desktop();
    }
    */
    return p->desktop;
}

int win_info::pid() const
{
    return p->pid;
}

xcb_timestamp_t win_info::userTime() const
{
    return p->user_time;
}

const char* win_info::startupId() const
{
    return p->startup_id;
}

unsigned long win_info::opacity() const
{
    return p->opacity;
}

qreal win_info::opacityF() const
{
    if (p->opacity == 0xffffffff) {
        return 1.0;
    }
    return p->opacity * 1.0 / 0xffffffff;
}

net::Actions win_info::allowedActions() const
{
    return p->allowed_actions;
}

bool win_info::hasNETSupport() const
{
    return p->has_net_support;
}

xcb_window_t win_info::transientFor() const
{
    return p->transient_for;
}

xcb_window_t win_info::groupLeader() const
{
    return p->window_group;
}

bool win_info::urgency() const
{
    return p->urgency;
}

bool win_info::input() const
{
    return p->input;
}

net::MappingState win_info::initialMappingState() const
{
    return p->initialMappingState;
}

xcb_pixmap_t win_info::icccmIconPixmap() const
{
    return p->icon_pixmap;
}

xcb_pixmap_t win_info::icccmIconPixmapMask() const
{
    return p->icon_mask;
}

const char* win_info::windowClassClass() const
{
    return p->class_class;
}

const char* win_info::windowClassName() const
{
    return p->class_name;
}

const char* win_info::windowRole() const
{
    return p->window_role;
}

const char* win_info::clientMachine() const
{
    return p->client_machine;
}

void win_info::setBlockingCompositing(bool active)
{
    assert(p->role == Client);

    p->blockCompositing = active;
    if (active) {
        uint32_t d = 1;
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_KDE_NET_WM_BLOCK_COMPOSITING),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            (const void*)&d);
        xcb_change_property(p->conn,
                            XCB_PROP_MODE_REPLACE,
                            p->window,
                            p->atom(_NET_WM_BYPASS_COMPOSITOR),
                            XCB_ATOM_CARDINAL,
                            32,
                            1,
                            (const void*)&d);
    } else {
        xcb_delete_property(p->conn, p->window, p->atom(_KDE_NET_WM_BLOCK_COMPOSITING));
        xcb_delete_property(p->conn, p->window, p->atom(_NET_WM_BYPASS_COMPOSITOR));
    }
}

bool win_info::isBlockingCompositing() const
{
    return p->blockCompositing;
}

bool win_info::handledIcons() const
{
    return p->handled_icons;
}

net::Properties win_info::passedProperties() const
{
    return p->properties;
}

net::Properties2 win_info::passedProperties2() const
{
    return p->properties2;
}

net::MappingState win_info::mappingState() const
{
    return p->mapping_state;
}

net::Protocols win_info::protocols() const
{
    return p->protocols;
}

bool win_info::supportsProtocol(net::Protocol protocol) const
{
    return p->protocols.testFlag(protocol);
}

std::vector<net::rect> win_info::opaqueRegion() const
{
    return p->opaqueRegion;
}

xcb_connection_t* win_info::xcbConnection() const
{
    return p->conn;
}

void win_info::setDesktopFileName(const char* name)
{
    assert(p->role == Client);

    delete[] p->desktop_file;
    p->desktop_file = nstrdup(name);

    xcb_change_property(p->conn,
                        XCB_PROP_MODE_REPLACE,
                        p->window,
                        p->atom(_KDE_NET_WM_DESKTOP_FILE),
                        p->atom(UTF8_STRING),
                        8,
                        strlen(p->desktop_file),
                        (const void*)p->desktop_file);
}

const char* win_info::desktopFileName() const
{
    return p->desktop_file;
}

const char* win_info::gtkApplicationId() const
{
    return p->gtk_application_id;
}

void win_info::virtual_hook(int, void*)
{
    /*BASE::virtual_hook( id, data );*/
}

int timestampCompare(unsigned long time1_, unsigned long time2_)
{
    quint32 time1 = time1_;
    quint32 time2 = time2_;
    if (time1 == time2) {
        return 0;
    }
    return quint32(time1 - time2) < 0x7fffffffU ? 1 : -1; // time1 > time2 -> 1, handle wrapping
}

}
