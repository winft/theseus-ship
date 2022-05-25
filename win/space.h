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
class QStringList;

namespace KWin
{

class RuleBook;

namespace base
{

namespace dbus
{
class kwin;
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
class stacking_tree;
}

enum class activation;
class app_menu;
class focus_chain;
class internal_window;
class kill_window;
class screen_edge;
class screen_edger;
class shortcut_dialog;
class stacking_order;
class tabbox;
class user_actions_menu;
class virtual_desktop_manager;

class KWIN_EXPORT space : public QObject
{
    Q_OBJECT
public:
    std::vector<Toplevel*> m_windows;

    std::unique_ptr<scripting::platform> scripting;
    std::unique_ptr<render::outline> outline;
    std::unique_ptr<win::screen_edger> edges;

    render::compositor& render;
    KStartupInfo* startup{nullptr};
    std::unique_ptr<base::x11::atoms> atoms;
    std::unique_ptr<deco::bridge<space>> deco;
    std::unique_ptr<win::app_menu> app_menu;
    std::unique_ptr<input::redirect> input;
    std::unique_ptr<win::tabbox> tabbox;
    std::unique_ptr<RuleBook> rule_book;
    std::unique_ptr<x11::color_mapper> color_mapper;

    QScopedPointer<base::x11::event_filter> m_wasUserInteractionFilter;
    QScopedPointer<base::x11::event_filter> m_movingClientFilter;
    QScopedPointer<base::x11::event_filter> m_syncAlarmFilter;

    int m_initialDesktop{1};
    QScopedPointer<base::x11::xcb::window> m_nullFocus;
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
    std::unique_ptr<win::user_actions_menu> user_actions_menu;

    static space* _self;

    explicit space(render::compositor& render);
    ~space() override;

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

    QRect clientArea(clientAreaOption, const QPoint& p, int desktop) const;
    QRect clientArea(clientAreaOption, Toplevel const* window) const;
    QRect clientArea(clientAreaOption, base::output const* output, int desktop) const;

    QRegion restrictedMoveArea(int desktop, win::strut_area areas = win::strut_area::all) const;

    void initShortcuts();
    bool initializing() const;

    /**
     * Returns the active client, i.e. the client that has the focus (or None
     * if no client has the focus)
     */
    Toplevel* activeClient() const;
    /**
     * Client that was activated, but it's not yet really activeClient(), because
     * we didn't process yet the matching FocusIn event. Used mostly in focus
     * stealing prevention code.
     */
    Toplevel* mostRecentlyActivatedClient() const;

    Toplevel* clientUnderMouse(base::output const* output) const;

    void activateClient(Toplevel* window, bool force = false);

    /**
     * Request focus and optionally try raising the window.
     * @param window The window to focus.
     * @param raise Should additionally raise the window.
     * @param force_focus Focus even if panel, dock and so on.
     */
    void request_focus(Toplevel* window, bool raise = false, bool force_focus = false);

    bool allowClientActivation(Toplevel const* window,
                               xcb_timestamp_t time = -1U,
                               bool focus_in = false,
                               bool ignore_desktop = false);
    void restoreFocus();
    void gotFocusIn(Toplevel const* window);
    void setShouldGetFocus(Toplevel* window);
    bool activateNextClient(Toplevel* window);
    bool focusChangeEnabled()
    {
        return block_focus == 0;
    }

    /**
     * Indicates that the client c is being moved or resized by the user.
     */
    void setMoveResizeClient(Toplevel* window);

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
    std::unique_ptr<win::x11::stacking_tree> x_stacking_tree;
    std::unique_ptr<win::focus_chain> focus_chain;
    std::unique_ptr<win::virtual_desktop_manager> virtual_desktop_manager;
    std::unique_ptr<base::dbus::kwin> dbus;
    std::unique_ptr<win::session_manager> session_manager;

    void stopUpdateToolWindowsTimer();
    void resetUpdateToolWindowsTimer();

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
    QRegion previousRestrictedMoveArea(int desktop,
                                       win::strut_area areas = win::strut_area::all) const;
    std::vector<QRect> previousScreenSizes() const;
    int oldDisplayWidth() const;
    int oldDisplayHeight() const;

    Toplevel* active_client{nullptr};

    void sendClientToDesktop(Toplevel* window, int desktop, bool dont_activate);
    void windowToPreviousDesktop(Toplevel* window);
    void windowToNextDesktop(Toplevel* window);

    /**
     * Shows the menu operations menu for the client and makes it active if
     * it's not already.
     */
    void showWindowMenu(const QRect& pos, Toplevel* window);

    void showApplicationMenu(const QRect& pos, Toplevel* window, int actionId);

    void updateMinimizedOfTransients(Toplevel*);
    void updateOnAllDesktopsOfTransients(Toplevel* window);
    void checkTransients(Toplevel* window);

    void storeSession(const QString& sessionName, win::sm_save_phase phase);
    void storeClient(KConfigGroup& cg, int num, win::x11::window* c);
    void storeSubSession(const QString& name, QSet<QByteArray> sessionIds);
    void loadSubSessionInfo(const QString& name);

    win::session_info* takeSessionInfo(win::x11::window*);

    // D-Bus interface
    QString supportInformation() const;

    void setCurrentScreen(base::output const& output);

    void setShowingDesktop(bool showing);
    bool showingDesktop() const;

    void setActiveClient(Toplevel* window);
    win::x11::group* findGroup(xcb_window_t leader) const;
    void addGroup(win::x11::group* group);
    void removeGroup(win::x11::group* group);

    bool checkStartupNotification(xcb_window_t w, KStartupInfoId& id, KStartupInfoData& data);

    void focusToNull(); // SELI TODO: Public?

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

    void cancelDelayFocus();
    void requestDelayFocus(Toplevel*);

    /**
     * updates the mouse position to track whether a focus follow mouse focus change was caused by
     * an actual mouse move
     * is esp. called on enter/motion events of inactive windows
     * since an active window doesn't receive mouse events, it must also be invoked if a
     * (potentially) active window might be moved/resize away from the cursor (causing a leave
     * event)
     */
    void updateFocusMousePosition(const QPoint& pos);
    QPoint focusMousePosition() const;

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

    enum Direction { DirectionNorth, DirectionEast, DirectionSouth, DirectionWest };
    void switchWindow(Direction direction);

    win::shortcut_dialog* shortcutDialog() const
    {
        return client_keys_dialog;
    }

    virtual win::screen_edge* create_screen_edge(win::screen_edger& edger);
    virtual QRect get_icon_geometry(Toplevel const* win) const;

    void fixPositionAfterCrash(xcb_window_t w, const xcb_get_geometry_reply_t* geom);
    void saveOldScreenSizes();
    void desktopResized();
    void closeActivePopup();

public Q_SLOTS:
    void performWindowOperation(KWin::Toplevel* window, base::options::WindowOperation op);
    // Keybindings
    // void slotSwitchToWindow( int );
    void slotWindowToDesktop(uint i);

    // void slotWindowToListPosition( int );
    void slotSwitchToScreen();
    void slotWindowToScreen();
    void slotSwitchToNextScreen();
    void slotWindowToNextScreen();
    void slotSwitchToPrevScreen();
    void slotWindowToPrevScreen();
    void slotToggleShowDesktop();

    void slotWindowMaximize();
    void slotWindowMaximizeVertical();
    void slotWindowMaximizeHorizontal();
    void slotWindowMinimize();
    void slotWindowRaise();
    void slotWindowLower();
    void slotWindowRaiseOrLower();
    void slotActivateAttentionWindow();
    void slotWindowPackLeft();
    void slotWindowPackRight();
    void slotWindowPackUp();
    void slotWindowPackDown();
    void slotWindowGrowHorizontal();
    void slotWindowGrowVertical();
    void slotWindowShrinkHorizontal();
    void slotWindowShrinkVertical();

    void slotIncreaseWindowOpacity();
    void slotLowerWindowOpacity();

    void slotWindowOperations();
    void slotWindowClose();
    void slotWindowMove();
    void slotWindowResize();
    void slotWindowAbove();
    void slotWindowBelow();
    void slotWindowOnAllDesktops();
    void slotWindowFullScreen();
    void slotWindowNoBorder();

    void slotWindowToNextDesktop();
    void slotWindowToPreviousDesktop();
    void slotWindowToDesktopRight();
    void slotWindowToDesktopLeft();
    void slotWindowToDesktopUp();
    void slotWindowToDesktopDown();

    void reconfigure();
    void slotReconfigure();

    void slotKillWindow();

    void slotSetupWindowShortcut();
    void setupWindowShortcutDone(bool);

    void updateClientArea();

protected:
    virtual void update_space_area_from_windows(QRect const& desktop_area,
                                                std::vector<QRect> const& screens_geos,
                                                win::space_areas& areas);

private Q_SLOTS:
    void slotUpdateToolWindows();
    void delayFocus();
    void slotReloadConfig();

    // virtual desktop handling
    void slotDesktopCountChanged(uint previousCount, uint newCount);
    void slotCurrentDesktopChanged(uint oldDesktop, uint newDesktop);

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
    void groupAdded(KWin::win::x11::group*);
    void unmanagedAdded(KWin::Toplevel*);
    void unmanagedRemoved(KWin::Toplevel*);
    void window_deleted(KWin::Toplevel*);
    void configChanged();
    void showingDesktopChanged(bool showing);

    /**
     * This signal is emitted whenever an internal client is created.
     */
    void internalClientAdded(KWin::win::internal_window* client);

    /**
     * This signal is emitted whenever an internal client gets removed.
     */
    void internalClientRemoved(KWin::win::internal_window* client);

    void surface_id_changed(KWin::Toplevel*, quint32);

private:
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
    void setupWindowShortcut(Toplevel* window);
    bool switchWindow(Toplevel* c, Direction direction, QPoint curPos, int desktop);

    void updateClientArea(bool force);
    void resetClientAreas(uint desktopCount);
    void activateClientOnNewDesktop(uint desktop);
    Toplevel* findClientToActivateOnDesktop(uint desktop);

    QWidget* active_popup{nullptr};

    void loadSessionInfo(const QString& sessionName);
    void addSessionInfo(KConfigGroup& cg);

    std::vector<win::session_info*> session;

    Toplevel* movingClient{nullptr};

    // Delay(ed) window focus timer and client
    QTimer* delayFocusTimer{nullptr};
    QPoint focusMousePos;

    bool showing_desktop{false};

    std::vector<win::x11::group*> groups;

    bool was_user_interaction{false};

    int session_active_client;
    int session_desktop;

    void modalActionsSwitch(bool enabled);

    win::shortcut_dialog* client_keys_dialog{nullptr};
    bool global_shortcuts_disabled_for_client{false};

    // Timer to collect requests for 'reconfigure'
    QTimer reconfigureTimer;

    QTimer updateToolWindowsTimer;

    win::space_areas areas;

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<win::strut_rects> oldrestrictedmovearea;

    // array of previous sizes of xinerama screens
    std::vector<QRect> oldscreensizes;

    // previous sizes od displayWidth()/displayHeight()
    QSize olddisplaysize;

    int set_active_client_recursion{0};

    std::unique_ptr<win::kill_window> m_windowKiller;

private:
    friend bool performTransiencyCheck();
};

inline Toplevel* space::activeClient() const
{
    return active_client;
}

inline Toplevel* space::mostRecentlyActivatedClient() const
{
    return should_get_focus.size() > 0 ? should_get_focus.back() : active_client;
}

inline void space::addGroup(win::x11::group* group)
{
    Q_EMIT groupAdded(group);
    groups.push_back(group);
}

inline void space::removeGroup(win::x11::group* group)
{
    remove_all(groups, group);
}

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

inline void space::updateFocusMousePosition(const QPoint& pos)
{
    focusMousePos = pos;
}

inline QPoint space::focusMousePosition() const
{
    return focusMousePos;
}

}

inline win::space* workspace()
{
    return win::space::_self;
}

}
