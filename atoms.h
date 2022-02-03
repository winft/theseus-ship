/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xcbutils.h"

namespace KWin
{

class Atoms
{
public:
    Atoms()
        : kwin_running(QByteArrayLiteral("KWIN_RUNNING"))
        , wm_protocols(QByteArrayLiteral("WM_PROTOCOLS"))
        , wm_delete_window(QByteArrayLiteral("WM_DELETE_WINDOW"))
        , wm_take_focus(QByteArrayLiteral("WM_TAKE_FOCUS"))
        , wm_change_state(QByteArrayLiteral("WM_CHANGE_STATE"))
        , wm_client_leader(QByteArrayLiteral("WM_CLIENT_LEADER"))
        , wm_window_role(QByteArrayLiteral("WM_WINDOW_ROLE"))
        , wm_state(QByteArrayLiteral("WM_STATE"))
        , sm_client_id(QByteArrayLiteral("SM_CLIENT_ID"))
        , motif_wm_hints(QByteArrayLiteral("_MOTIF_WM_HINTS"))
        , net_wm_context_help(QByteArrayLiteral("_NET_WM_CONTEXT_HELP"))
        , net_wm_ping(QByteArrayLiteral("_NET_WM_PING"))
        , net_wm_user_time(QByteArrayLiteral("_NET_WM_USER_TIME"))
        , kde_net_wm_user_creation_time(QByteArrayLiteral("_KDE_NET_WM_USER_CREATION_TIME"))
        , net_wm_window_opacity(QByteArrayLiteral("_NET_WM_WINDOW_OPACITY"))
        , xdnd_selection(QByteArrayLiteral("XdndSelection"))
        , xdnd_aware(QByteArrayLiteral("XdndAware"))
        , xdnd_enter(QByteArrayLiteral("XdndEnter"))
        , xdnd_type_list(QByteArrayLiteral("XdndTypeList"))
        , xdnd_position(QByteArrayLiteral("XdndPosition"))
        , xdnd_status(QByteArrayLiteral("XdndStatus"))
        , xdnd_action_copy(QByteArrayLiteral("XdndActionCopy"))
        , xdnd_action_move(QByteArrayLiteral("XdndActionMove"))
        , xdnd_action_ask(QByteArrayLiteral("XdndActionAsk"))
        , xdnd_drop(QByteArrayLiteral("XdndDrop"))
        , xdnd_leave(QByteArrayLiteral("XdndLeave"))
        , xdnd_finished(QByteArrayLiteral("XdndFinished"))
        , net_frame_extents(QByteArrayLiteral("_NET_FRAME_EXTENTS"))
        , kde_net_wm_frame_strut(QByteArrayLiteral("_KDE_NET_WM_FRAME_STRUT"))
        , net_wm_sync_request_counter(QByteArrayLiteral("_NET_WM_SYNC_REQUEST_COUNTER"))
        , net_wm_sync_request(QByteArrayLiteral("_NET_WM_SYNC_REQUEST"))
        , kde_net_wm_shadow(QByteArrayLiteral("_KDE_NET_WM_SHADOW"))
        , kde_first_in_window_list(QByteArrayLiteral("_KDE_FIRST_IN_WINDOWLIST"))
        , kde_color_sheme(QByteArrayLiteral("_KDE_NET_WM_COLOR_SCHEME"))
        , kde_skip_close_animation(QByteArrayLiteral("_KDE_NET_WM_SKIP_CLOSE_ANIMATION"))
        , kde_screen_edge_show(QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"))
        , kwin_dbus_service(QByteArrayLiteral("_ORG_KDE_KWIN_DBUS_SERVICE"))
        , utf8_string(QByteArrayLiteral("UTF8_STRING"))
        , text(QByteArrayLiteral("TEXT"))
        , uri_list(QByteArrayLiteral("text/uri-list"))
        , netscape_url(QByteArrayLiteral("_NETSCAPE_URL"))
        , moz_url(QByteArrayLiteral("text/x-moz-url"))
        , wl_surface_id(QByteArrayLiteral("WL_SURFACE_ID"))
        , kde_net_wm_appmenu_service_name(QByteArrayLiteral("_KDE_NET_WM_APPMENU_SERVICE_NAME"))
        , kde_net_wm_appmenu_object_path(QByteArrayLiteral("_KDE_NET_WM_APPMENU_OBJECT_PATH"))
        , clipboard(QByteArrayLiteral("CLIPBOARD"))
        , timestamp(QByteArrayLiteral("TIMESTAMP"))
        , targets(QByteArrayLiteral("TARGETS"))
        , delete_atom(QByteArrayLiteral("DELETE"))
        , incr(QByteArrayLiteral("INCR"))
        , wl_selection(QByteArrayLiteral("WL_SELECTION"))
        , primary_selection(QByteArrayLiteral("PRIMARY"))
        , xwayland_randr_emu_monitor_rects(QByteArrayLiteral("_XWAYLAND_RANDR_EMU_MONITOR_RECTS"))
        , m_dtSmWindowInfo(QByteArrayLiteral("_DT_SM_WINDOW_INFO"))
        , m_motifSupport(QByteArrayLiteral("_MOTIF_WM_INFO"))
        , m_helpersRetrieved(false)
    {
    }

    Xcb::Atom kwin_running;
    Xcb::Atom wm_protocols;
    Xcb::Atom wm_delete_window;
    Xcb::Atom wm_take_focus;
    Xcb::Atom wm_change_state;
    Xcb::Atom wm_client_leader;
    Xcb::Atom wm_window_role;
    Xcb::Atom wm_state;
    Xcb::Atom sm_client_id;

    Xcb::Atom motif_wm_hints;
    Xcb::Atom net_wm_context_help;
    Xcb::Atom net_wm_ping;
    Xcb::Atom net_wm_user_time;
    Xcb::Atom kde_net_wm_user_creation_time;
    Xcb::Atom net_wm_window_opacity;
    Xcb::Atom xdnd_selection;
    Xcb::Atom xdnd_aware;
    Xcb::Atom xdnd_enter;
    Xcb::Atom xdnd_type_list;
    Xcb::Atom xdnd_position;
    Xcb::Atom xdnd_status;
    Xcb::Atom xdnd_action_copy;
    Xcb::Atom xdnd_action_move;
    Xcb::Atom xdnd_action_ask;
    Xcb::Atom xdnd_drop;
    Xcb::Atom xdnd_leave;
    Xcb::Atom xdnd_finished;
    Xcb::Atom net_frame_extents;
    Xcb::Atom kde_net_wm_frame_strut;
    Xcb::Atom net_wm_sync_request_counter;
    Xcb::Atom net_wm_sync_request;
    Xcb::Atom kde_net_wm_shadow;
    Xcb::Atom kde_first_in_window_list;
    Xcb::Atom kde_color_sheme;
    Xcb::Atom kde_skip_close_animation;
    Xcb::Atom kde_screen_edge_show;
    Xcb::Atom kwin_dbus_service;
    Xcb::Atom utf8_string;
    Xcb::Atom text;
    Xcb::Atom uri_list;
    Xcb::Atom netscape_url;
    Xcb::Atom moz_url;
    Xcb::Atom wl_surface_id;
    Xcb::Atom kde_net_wm_appmenu_service_name;
    Xcb::Atom kde_net_wm_appmenu_object_path;
    Xcb::Atom clipboard;
    Xcb::Atom timestamp;
    Xcb::Atom targets;
    Xcb::Atom delete_atom;
    Xcb::Atom incr;
    Xcb::Atom wl_selection;
    Xcb::Atom primary_selection;
    Xcb::Atom xwayland_randr_emu_monitor_rects;

    /**
     * @internal
     */
    void retrieveHelpers()
    {
        if (m_helpersRetrieved) {
            return;
        }

        // just retrieve the atoms once, all others are retrieved when being accessed
        // [[maybe_unused]] is used hoping that the compiler doesn't optimize the operations away
        [[maybe_unused]] xcb_atom_t atom = m_dtSmWindowInfo;
        atom = m_motifSupport;

        m_helpersRetrieved = true;
    }

private:
    // helper atoms we need to resolve to "announce" support (see #172028)
    Xcb::Atom m_dtSmWindowInfo;
    Xcb::Atom m_motifSupport;
    bool m_helpersRetrieved;
};

extern KWIN_EXPORT Atoms* atoms;

}
