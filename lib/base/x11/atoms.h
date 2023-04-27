/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xcb/atom.h"

namespace KWin::base::x11
{

class atoms
{
public:
    explicit atoms(xcb_connection_t* con)
        : wm_protocols{QByteArrayLiteral("WM_PROTOCOLS"), con}
        , wm_delete_window{QByteArrayLiteral("WM_DELETE_WINDOW"), con}
        , wm_take_focus{QByteArrayLiteral("WM_TAKE_FOCUS"), con}
        , wm_change_state{QByteArrayLiteral("WM_CHANGE_STATE"), con}
        , wm_client_leader{QByteArrayLiteral("WM_CLIENT_LEADER"), con}
        , wm_window_role{QByteArrayLiteral("WM_WINDOW_ROLE"), con}
        , wm_state{QByteArrayLiteral("WM_STATE"), con}
        , sm_client_id{QByteArrayLiteral("SM_CLIENT_ID"), con}
        , motif_wm_hints{QByteArrayLiteral("_MOTIF_WM_HINTS"), con}
        , net_wm_context_help{QByteArrayLiteral("_NET_WM_CONTEXT_HELP"), con}
        , net_wm_ping{QByteArrayLiteral("_NET_WM_PING"), con}
        , net_wm_user_time{QByteArrayLiteral("_NET_WM_USER_TIME"), con}
        , kde_net_wm_user_creation_time{QByteArrayLiteral("_KDE_NET_WM_USER_CREATION_TIME"), con}
        , net_wm_window_opacity{QByteArrayLiteral("_NET_WM_WINDOW_OPACITY"), con}
        , xdnd_selection{QByteArrayLiteral("XdndSelection"), con}
        , xdnd_aware{QByteArrayLiteral("XdndAware"), con}
        , xdnd_enter{QByteArrayLiteral("XdndEnter"), con}
        , xdnd_type_list{QByteArrayLiteral("XdndTypeList"), con}
        , xdnd_position{QByteArrayLiteral("XdndPosition"), con}
        , xdnd_status{QByteArrayLiteral("XdndStatus"), con}
        , xdnd_action_copy{QByteArrayLiteral("XdndActionCopy"), con}
        , xdnd_action_move{QByteArrayLiteral("XdndActionMove"), con}
        , xdnd_action_ask{QByteArrayLiteral("XdndActionAsk"), con}
        , xdnd_drop{QByteArrayLiteral("XdndDrop"), con}
        , xdnd_leave{QByteArrayLiteral("XdndLeave"), con}
        , xdnd_finished{QByteArrayLiteral("XdndFinished"), con}
        , net_frame_extents{QByteArrayLiteral("_NET_FRAME_EXTENTS"), con}
        , kde_net_wm_frame_strut{QByteArrayLiteral("_KDE_NET_WM_FRAME_STRUT"), con}
        , net_wm_sync_request_counter{QByteArrayLiteral("_NET_WM_SYNC_REQUEST_COUNTER"), con}
        , net_wm_sync_request{QByteArrayLiteral("_NET_WM_SYNC_REQUEST"), con}
        , kde_net_wm_shadow{QByteArrayLiteral("_KDE_NET_WM_SHADOW"), con}
        , kde_color_sheme{QByteArrayLiteral("_KDE_NET_WM_COLOR_SCHEME"), con}
        , kde_skip_close_animation{QByteArrayLiteral("_KDE_NET_WM_SKIP_CLOSE_ANIMATION"), con}
        , kde_screen_edge_show{QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), con}
        , utf8_string{QByteArrayLiteral("UTF8_STRING"), con}
        , text{QByteArrayLiteral("TEXT"), con}
        , uri_list{QByteArrayLiteral("text/uri-list"), con}
        , netscape_url{QByteArrayLiteral("_NETSCAPE_URL"), con}
        , moz_url{QByteArrayLiteral("text/x-moz-url"), con}
        , wl_surface_id{QByteArrayLiteral("WL_SURFACE_ID"), con}
        , kde_net_wm_appmenu_service_name{QByteArrayLiteral("_KDE_NET_WM_APPMENU_SERVICE_NAME"),
                                          con}
        , kde_net_wm_appmenu_object_path{QByteArrayLiteral("_KDE_NET_WM_APPMENU_OBJECT_PATH"), con}
        , clipboard{QByteArrayLiteral("CLIPBOARD"), con}
        , timestamp{QByteArrayLiteral("TIMESTAMP"), con}
        , targets{QByteArrayLiteral("TARGETS"), con}
        , delete_atom{QByteArrayLiteral("DELETE"), con}
        , incr{QByteArrayLiteral("INCR"), con}
        , wl_selection{QByteArrayLiteral("WL_SELECTION"), con}
        , primary_selection{QByteArrayLiteral("PRIMARY"), con}
        , xwayland_randr_emu_monitor_rects{QByteArrayLiteral("_XWAYLAND_RANDR_EMU_MONITOR_RECTS"),
                                           con}
        , m_dtSmWindowInfo{QByteArrayLiteral("_DT_SM_WINDOW_INFO"), con}
        , m_motifSupport{QByteArrayLiteral("_MOTIF_WM_INFO"), con}
        , m_helpersRetrieved(false)
    {
    }

    xcb::atom wm_protocols;
    xcb::atom wm_delete_window;
    xcb::atom wm_take_focus;
    xcb::atom wm_change_state;
    xcb::atom wm_client_leader;
    xcb::atom wm_window_role;
    xcb::atom wm_state;
    xcb::atom sm_client_id;

    xcb::atom motif_wm_hints;
    xcb::atom net_wm_context_help;
    xcb::atom net_wm_ping;
    xcb::atom net_wm_user_time;
    xcb::atom kde_net_wm_user_creation_time;
    xcb::atom net_wm_window_opacity;
    xcb::atom xdnd_selection;
    xcb::atom xdnd_aware;
    xcb::atom xdnd_enter;
    xcb::atom xdnd_type_list;
    xcb::atom xdnd_position;
    xcb::atom xdnd_status;
    xcb::atom xdnd_action_copy;
    xcb::atom xdnd_action_move;
    xcb::atom xdnd_action_ask;
    xcb::atom xdnd_drop;
    xcb::atom xdnd_leave;
    xcb::atom xdnd_finished;
    xcb::atom net_frame_extents;
    xcb::atom kde_net_wm_frame_strut;
    xcb::atom net_wm_sync_request_counter;
    xcb::atom net_wm_sync_request;
    xcb::atom kde_net_wm_shadow;
    xcb::atom kde_color_sheme;
    xcb::atom kde_skip_close_animation;
    xcb::atom kde_screen_edge_show;
    xcb::atom utf8_string;
    xcb::atom text;
    xcb::atom uri_list;
    xcb::atom netscape_url;
    xcb::atom moz_url;
    xcb::atom wl_surface_id;
    xcb::atom kde_net_wm_appmenu_service_name;
    xcb::atom kde_net_wm_appmenu_object_path;
    xcb::atom clipboard;
    xcb::atom timestamp;
    xcb::atom targets;
    xcb::atom delete_atom;
    xcb::atom incr;
    xcb::atom wl_selection;
    xcb::atom primary_selection;
    xcb::atom xwayland_randr_emu_monitor_rects;

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
    xcb::atom m_dtSmWindowInfo;
    xcb::atom m_motifSupport;
    bool m_helpersRetrieved;
};

}
