/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "appmenu.h"
#include "dbus/appmenu.h"
#include "dbus/virtual_desktop_manager.h"
#include "deco/bridge.h"
#include "focus_chain.h"
#include "kill_window.h"
#include "osd_notification.h"
#include "screen_edges.h"
#include "session_manager.h"
#include "space_areas.h"
#include "space_qobject.h"
#include "space_reconfigure.h"
#include "space_setup.h"
#include "stacking_order.h"
#include "strut_rect.h"
#include "user_actions_menu.h"

#include "base/dbus/kwin.h"
#include "base/output.h"
#include "base/x11/atoms.h"
#include "base/x11/event_filter.h"
#include "input/redirect.h"
#include "render/outline.h"
#include "rules/book.h"
#include "scripting/platform.h"

#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

class KStartupInfo;

namespace KWin
{

namespace render
{
class compositor;
}

class Toplevel;

namespace win
{

class shortcut_dialog;
class tabbox;

class space
{
public:
    using qobject_t = space_qobject;

    explicit space(render::compositor& render)
        : qobject{std::make_unique<space_qobject>([this] { space_start_reconfigure_timer(*this); })}
        , outline{std::make_unique<render::outline>(render)}
        , render{render}
        , deco{std::make_unique<deco::bridge<space>>(*this)}
        , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
        , rule_book{std::make_unique<rules::book>()}
        , user_actions_menu{std::make_unique<win::user_actions_menu<space>>(*this)}
        , stacking_order{std::make_unique<win::stacking_order>()}
        , focus_chain{win::focus_chain<space>(*this)}
        , virtual_desktop_manager{std::make_unique<win::virtual_desktop_manager>()}
        , session_manager{std::make_unique<win::session_manager>()}
    {
    }

    virtual ~space()
    {
        clear_space(*this);
    }

    virtual void resize(QSize const& size) = 0;

    /**
     * @brief Finds a Toplevel for the internal window @p w.
     *
     * Internal window means a window created by KWin itself. On X11 this is an Unmanaged
     * and mapped by the window id, on Wayland a XdgShellClient mapped on the internal window id.
     *
     * @returns Toplevel
     */
    virtual Toplevel* findInternal(QWindow* w) const = 0;

    virtual win::screen_edge* create_screen_edge(win::screen_edger& edger)
    {
        return new win::screen_edge(&edger);
    }

    virtual QRect get_icon_geometry(Toplevel const* /*win*/) const
    {
        return {};
    }

    virtual void update_space_area_from_windows(QRect const& /*desktop_area*/,
                                                std::vector<QRect> const& /*screens_geos*/,
                                                win::space_areas& /*areas*/)
    {
        // Can't be pure virtual because the function might be called from the ctor.
    }

    std::unique_ptr<qobject_t> qobject;

    std::vector<Toplevel*> windows;
    std::vector<win::x11::group*> groups;

    win::space_areas areas;

    std::unique_ptr<scripting::platform> scripting;
    std::unique_ptr<render::outline> outline;
    std::unique_ptr<win::screen_edger> edges;

    render::compositor& render;
    KStartupInfo* startup{nullptr};
    std::unique_ptr<base::x11::atoms> atoms;
    std::unique_ptr<deco::bridge<space>> deco;
    std::unique_ptr<dbus::appmenu> appmenu;
    std::unique_ptr<input::redirect> input;
    std::unique_ptr<win::tabbox> tabbox;
    std::unique_ptr<rules::book> rule_book;
    std::unique_ptr<x11::color_mapper> color_mapper;

    std::unique_ptr<base::x11::event_filter> m_wasUserInteractionFilter;
    std::unique_ptr<base::x11::event_filter> m_movingClientFilter;
    std::unique_ptr<base::x11::event_filter> m_syncAlarmFilter;

    int m_initialDesktop{1};
    std::unique_ptr<base::x11::xcb::window> m_nullFocus;
    Toplevel* active_popup_client{nullptr};

    Toplevel* last_active_client{nullptr};
    Toplevel* delayfocus_client{nullptr};
    Toplevel* client_keys_client{nullptr};

    // Last is most recent.
    std::deque<Toplevel*> should_get_focus;
    std::deque<Toplevel*> attention_chain;

    int block_focus{0};

    /**
     * Holds the menu containing the user actions which is shown
     * on e.g. right click the window decoration.
     */
    std::unique_ptr<win::user_actions_menu<space>> user_actions_menu;

    QPoint focusMousePos;

    // Timer to collect requests for 'reconfigure'
    QTimer reconfigureTimer;
    QTimer updateToolWindowsTimer;

    Toplevel* move_resize_window{nullptr};

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<win::strut_rects> oldrestrictedmovearea;

    /**
     * Most recently raised window.
     *
     * Accessed and modified by raise or lower client.
     */
    Toplevel* most_recently_raised{nullptr};

    std::unique_ptr<win::stacking_order> stacking_order;
    win::focus_chain<space> focus_chain;
    std::unique_ptr<win::virtual_desktop_manager> virtual_desktop_manager;
    std::unique_ptr<base::dbus::kwin_impl<space, input::platform>> dbus;
    std::unique_ptr<win::session_manager> session_manager;

    QTimer* m_quickTileCombineTimer{nullptr};
    win::quicktiles m_lastTilingMode{win::quicktiles::none};

    Toplevel* active_client{nullptr};

    QWidget* active_popup{nullptr};

    std::vector<win::session_info*> session;

    // Delay(ed) window focus timer and client
    QTimer* delayFocusTimer{nullptr};

    bool showing_desktop{false};
    bool was_user_interaction{false};

    int session_active_client;
    int session_desktop;

    win::shortcut_dialog* client_keys_dialog{nullptr};
    bool global_shortcuts_disabled{false};

    // array of previous sizes of xinerama screens
    std::vector<QRect> oldscreensizes;

    // previous sizes od displayWidth()/displayHeight()
    QSize olddisplaysize;

    int set_active_client_recursion{0};

    std::unique_ptr<osd_notification<input::redirect>> osd;
    std::unique_ptr<kill_window<space>> window_killer;
};

}

}
