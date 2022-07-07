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

#include "focus_chain.h"

#include "base/options.h"
#include "base/output.h"
#include "base/x11/atoms.h"
#include "utils/algorithm.h"
#include "win/session_manager.h"
#include "win/space_areas.h"
#include "win/strut_rect.h"

#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

class KConfig;
class KConfigGroup;
class KStartupInfo;
class KStartupInfoData;
class KStartupInfoId;
class QAction;
class QStringList;

namespace KWin
{

class RuleBook;

namespace base
{

namespace dbus
{
template<typename Space>
class kwin_impl;
}

namespace x11
{
namespace xcb
{
class tree;
class window;
}
class event_filter;
}

}

namespace input
{
class redirect;
}

namespace render
{
class compositor;
class outline;
}

namespace scripting
{
class platform;
}

class Toplevel;

namespace win
{

namespace dbus
{
class appmenu;
}

namespace deco
{
template<typename Space>
class bridge;
}

namespace x11
{
enum class predicate_match;
class color_mapper;
class window;
class group;
}

enum class activation;
class internal_window;

template<typename Space>
class kill_window;
class screen_edge;
class screen_edger;
class shortcut_dialog;
class stacking_order;
class tabbox;

template<typename Space>
class user_actions_menu;
class virtual_desktop_manager;

class space;

class KWIN_EXPORT space_qobject : public QObject
{
    Q_OBJECT
public:
    space_qobject(std::function<void()> reconfigure_callback);

public Q_SLOTS:
    void reconfigure();

Q_SIGNALS:
    void desktopPresenceChanged(KWin::Toplevel*, int);
    void currentDesktopChanged(int, KWin::Toplevel*);
    void clientAdded(KWin::win::x11::window*);
    void clientRemoved(KWin::Toplevel*);
    void wayland_window_added(KWin::Toplevel*);
    void wayland_window_removed(KWin::Toplevel*);
    void clientActivated(KWin::Toplevel*);
    void clientDemandsAttentionChanged(KWin::Toplevel*, bool);
    void clientMinimizedChanged(KWin::Toplevel*);
    void unmanagedAdded(KWin::Toplevel*);
    void unmanagedRemoved(KWin::Toplevel*);
    void window_deleted(KWin::Toplevel*);
    void configChanged();
    void showingDesktopChanged(bool showing);
    void internalClientAdded(KWin::win::internal_window* client);
    void internalClientRemoved(KWin::win::internal_window* client);
    void surface_id_changed(KWin::Toplevel*, quint32);

private:
    std::function<void()> reconfigure_callback;
};

class KWIN_EXPORT space
{
public:
    using qobject_t = space_qobject;
    std::unique_ptr<qobject_t> qobject;

    std::vector<Toplevel*> m_windows;
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
    std::unique_ptr<RuleBook> rule_book;
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

    Toplevel* movingClient{nullptr};

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<win::strut_rects> oldrestrictedmovearea;

    explicit space(render::compositor& render);
    virtual ~space();

    bool workspaceEvent(QEvent*);

    /**
     * @brief Finds a Toplevel for the internal window @p w.
     *
     * Internal window means a window created by KWin itself. On X11 this is an Unmanaged
     * and mapped by the window id, on Wayland a XdgShellClient mapped on the internal window id.
     *
     * @returns Toplevel
     */
    virtual Toplevel* findInternal(QWindow* w) const = 0;

    void initShortcuts();
    bool initializing() const;

    bool allowClientActivation(Toplevel const* window,
                               xcb_timestamp_t time = -1U,
                               bool focus_in = false,
                               bool ignore_desktop = false);
    void restoreFocus();
    void gotFocusIn(Toplevel const* window);
    void setShouldGetFocus(Toplevel* window);

    QPoint
    adjustClientPosition(Toplevel* window, QPoint pos, bool unrestricted, double snapAdjust = 1.0);
    QRect adjustClientSize(Toplevel* window, QRect moveResizeGeom, win::position mode);

    // used by layers.cpp, defined in activation.cpp
    bool allowFullClientRaising(Toplevel const* c, xcb_timestamp_t timestamp);

    /**
     * Most recently raised window.
     *
     * Accessed and modified by raise or lower client.
     */
    Toplevel* most_recently_raised{nullptr};

    std::unique_ptr<win::stacking_order> stacking_order;
    win::focus_chain<space> focus_chain;
    std::unique_ptr<win::virtual_desktop_manager> virtual_desktop_manager;
    std::unique_ptr<base::dbus::kwin_impl<space>> dbus;
    std::unique_ptr<win::session_manager> session_manager;

    void clientHidden(Toplevel* window);
    void clientAttentionChanged(Toplevel* window, bool set);

    std::vector<Toplevel*> const& windows() const;

    /**
     * @return List of unmanaged "clients" currently registered in space
     */
    std::vector<Toplevel*> unmanagedList() const;
    /**
     * @return Remnant windows, i.e. already closed but still kept around for closing effects.
     */
    std::vector<Toplevel*> remnants() const;

    void updateTabbox();

private:
    QTimer* m_quickTileCombineTimer{nullptr};
    win::quicktiles m_lastTilingMode{win::quicktiles::none};

    //-------------------------------------------------
    // Unsorted

public:
    // True when performing space::updateClientArea().
    // The calls below are valid only in that case.
    bool inUpdateClientArea() const;

    Toplevel* active_client{nullptr};

    void updateMinimizedOfTransients(Toplevel*);
    void updateOnAllDesktopsOfTransients(Toplevel* window);
    void checkTransients(Toplevel* window);

    bool showingDesktop() const;

    bool checkStartupNotification(xcb_window_t w, KStartupInfoId& id, KStartupInfoData& data);

    void clientShortcutUpdated(Toplevel* window);
    bool shortcutAvailable(const QKeySequence& cut, Toplevel* ignore = nullptr) const;
    bool globalShortcutsDisabled() const;
    void disableGlobalShortcutsForClient(bool disable);

    void setWasUserInteraction();
    bool wasUserInteraction() const;

    int packPositionLeft(Toplevel const* window, int oldX, bool leftEdge) const;
    int packPositionRight(Toplevel const* window, int oldX, bool rightEdge) const;
    int packPositionUp(Toplevel const* window, int oldY, bool topEdge) const;
    int packPositionDown(Toplevel const* window, int oldY, bool bottomEdge) const;

    /**
     * Returns a client that is currently being moved or resized by the user.
     *
     * If none of clients is being moved or resized, @c null will be returned.
     */
    Toplevel* moveResizeClient()
    {
        return movingClient;
    }

    /**
     * @returns Whether we have a compositor and it is active (Scene created)
     */
    bool compositing() const;

    void quickTileWindow(win::quicktiles mode);

    void switchWindow(win::direction direction);

    win::shortcut_dialog* shortcutDialog() const
    {
        return client_keys_dialog;
    }

    virtual win::screen_edge* create_screen_edge(win::screen_edger& edger);
    virtual QRect get_icon_geometry(Toplevel const* win) const;

    void fixPositionAfterCrash(xcb_window_t w, const xcb_get_geometry_reply_t* geom);
    void saveOldScreenSizes();
    void desktopResized();

    void setupWindowShortcutDone(bool);

    virtual void update_space_area_from_windows(QRect const& desktop_area,
                                                std::vector<QRect> const& screens_geos,
                                                win::space_areas& areas);

    template<typename Slot>
    void initShortcut(const QString& actionName,
                      const QString& description,
                      const QKeySequence& shortcut,
                      Slot slot,
                      const QVariant& data = QVariant());
    template<typename T, typename Slot>
    void initShortcut(const QString& actionName,
                      const QString& description,
                      const QKeySequence& shortcut,
                      T* receiver,
                      Slot slot,
                      const QVariant& data = QVariant());
    template<typename T, typename Slot>
    void init_shortcut_with_action_arg(const QString& actionName,
                                       const QString& description,
                                       const QKeySequence& shortcut,
                                       T* receiver,
                                       Slot slot,
                                       QVariant const& data);
    void setupWindowShortcut(Toplevel* window);
    bool switchWindow(Toplevel* c, win::direction direction, QPoint curPos, int desktop);

    QWidget* active_popup{nullptr};

    std::vector<win::session_info*> session;

    // Delay(ed) window focus timer and client
    QTimer* delayFocusTimer{nullptr};

    bool showing_desktop{false};
    bool was_user_interaction{false};

    int session_active_client;
    int session_desktop;

    void modalActionsSwitch(bool enabled);

    win::shortcut_dialog* client_keys_dialog{nullptr};
    bool global_shortcuts_disabled_for_client{false};

    // array of previous sizes of xinerama screens
    std::vector<QRect> oldscreensizes;

    // previous sizes od displayWidth()/displayHeight()
    QSize olddisplaysize;

    int set_active_client_recursion{0};

    std::unique_ptr<kill_window<space>> window_killer;

private:
    friend bool performTransiencyCheck();
};

inline bool space::wasUserInteraction() const
{
    return was_user_interaction;
}

inline bool space::showingDesktop() const
{
    return showing_desktop;
}

inline bool space::globalShortcutsDisabled() const
{
    return global_shortcuts_disabled_for_client;
}

}

}
