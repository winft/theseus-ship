/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
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
#include "space.h"

#include "activation.h"
#include "active_window.h"
#include "deco/bridge.h"
#include "desktop_space.h"
#include "output_space.h"
#include "session.h"
#include "singleton_interface.h"
#include "space_areas_helpers.h"
#include "space_setup.h"
#include "window_area.h"
#include "x11/tool_windows.h"

#include "base/dbus/kwin.h"
#include "base/output_helpers.h"
#include "base/x11/user_interaction_filter.h"
#include "base/x11/xcb/extensions.h"
#include "input/cursor.h"
#include "main.h"
#include "render/effects.h"
#include "render/outline.h"
#include "render/platform.h"
#include "render/post/night_color_manager.h"
#include "rules/rule_book.h"
#include "rules/rules.h"
#include "scripting/platform.h"
#include "utils/blocker.h"
#include "win/controlling.h"
#include "win/dbus/appmenu.h"
#include "win/dbus/virtual_desktop_manager.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/kill_window.h"
#include "win/layers.h"
#include "win/remnant.h"
#include "win/screen_edges.h"
#include "win/setup.h"
#include "win/shortcut_dialog.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/user_actions_menu.h"
#include "win/util.h"
#include "win/virtual_desktops.h"
#include "win/x11/control_create.h"
#include "win/x11/event.h"
#include "win/x11/group.h"
#include "win/x11/moving_window_filter.h"
#include "win/x11/netinfo.h"
#include "win/x11/space_areas.h"
#include "win/x11/space_setup.h"
#include "win/x11/stacking.h"
#include "win/x11/sync_alarm_filter.h"
#include "win/x11/transient.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window.h"

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

// TODO(romangg): For now this needs to be included late because of some conflict with Qt libraries.
#include "space_reconfigure.h"

#include <KStartupInfo>
#include <QAction>
#include <QtConcurrentRun>
#include <cassert>
#include <memory>

namespace KWin::win
{

space_qobject::space_qobject(std::function<void()> reconfigure_callback)
    : reconfigure_callback{reconfigure_callback}
{
}

void space_qobject::reconfigure()
{
    reconfigure_callback();
}

space::space(render::compositor& render)
    : qobject{std::make_unique<space_qobject>([this] { space_start_reconfigure_timer(*this); })}
    , outline{std::make_unique<render::outline>(render)}
    , render{render}
    , deco{std::make_unique<deco::bridge<space>>(*this)}
    , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
    , rule_book{std::make_unique<RuleBook>()}
    , user_actions_menu{std::make_unique<win::user_actions_menu<space>>(*this)}
    , stacking_order{std::make_unique<win::stacking_order>()}
    , focus_chain{win::focus_chain<space>(*this)}
    , virtual_desktop_manager{std::make_unique<win::virtual_desktop_manager>()}
    , dbus{std::make_unique<base::dbus::kwin_impl<space>>(*this)}
    , session_manager{std::make_unique<win::session_manager>()}
{
    init_space(*this);
}

space::~space()
{
    clear_space(*this);
}

win::screen_edge* space::create_screen_edge(win::screen_edger& edger)
{
    return new win::screen_edge(&edger);
}

QRect space::get_icon_geometry(Toplevel const* /*win*/) const
{
    return QRect();
}

void space::update_space_area_from_windows(QRect const& /*desktop_area*/,
                                           std::vector<QRect> const& /*screens_geos*/,
                                           win::space_areas& /*areas*/)
{
    // Can't be pure virtual because the function might be called from the ctor.
}

}
