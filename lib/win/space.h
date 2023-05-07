/*
SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

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
#include "x11/netinfo.h"

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

#include "base/output.h"
#include "base/x11/atoms.h"
#include "base/x11/event_filter.h"
#include "base/x11/xcb/window.h"
#include "render/outline.h"
#include "rules/book.h"

#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace KWin::win
{

template<typename Window>
struct stacking_state {
    win::stacking_order<Window> order;
    win::focus_chain<Window> focus_chain;

    // Last is most recent.
    std::deque<Window> should_get_focus;
    std::deque<Window> attention_chain;

    std::optional<Window> active;
    std::optional<Window> last_active;
    std::optional<Window> most_recently_raised;

    std::optional<Window> delayfocus_window;
};

class space
{
public:
    using qobject_t = space_qobject;

    space(KSharedConfigPtr config)
        : qobject{std::make_unique<space_qobject>([this] { space_start_reconfigure_timer(*this); })}
        , options{std::make_unique<win::options>(config)}
        , rule_book{std::make_unique<rules::book>()}
        , virtual_desktop_manager{std::make_unique<win::virtual_desktop_manager>()}
        , session_manager{std::make_unique<win::session_manager>()}
    {
    }

    virtual ~space()
    {
        singleton_interface::get_current_output_geometry = {};
    }

    virtual void resize(QSize const& size) = 0;
    virtual void handle_desktop_changed(uint desktop) = 0;

    virtual void update_space_area_from_windows(QRect const& /*desktop_area*/,
                                                std::vector<QRect> const& /*screens_geos*/,
                                                win::space_areas& /*areas*/)
    {
        // Can't be pure virtual because the function might be called from the ctor.
    }

    virtual void show_debug_console() = 0;

    std::unique_ptr<qobject_t> qobject;
    std::unique_ptr<win::options> options;

    win::space_areas areas;
    std::unique_ptr<base::x11::atoms> atoms;
    std::unique_ptr<rules::book> rule_book;

    std::unique_ptr<base::x11::event_filter> m_wasUserInteractionFilter;
    std::unique_ptr<base::x11::event_filter> m_movingClientFilter;
    std::unique_ptr<base::x11::event_filter> m_syncAlarmFilter;

    int m_initialDesktop{1};
    std::unique_ptr<base::x11::xcb::window> m_nullFocus;

    int block_focus{0};

    QPoint focusMousePos;

    // Timer to collect requests for 'reconfigure'
    QTimer reconfigureTimer;
    QTimer updateToolWindowsTimer;

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<win::strut_rects> oldrestrictedmovearea;

    std::unique_ptr<win::virtual_desktop_manager> virtual_desktop_manager;
    std::unique_ptr<win::session_manager> session_manager;

    QTimer* m_quickTileCombineTimer{nullptr};
    win::quicktiles m_lastTilingMode{win::quicktiles::none};

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

    base::x11::xcb::window shape_helper_window;

    uint32_t window_id{0};
};

}
