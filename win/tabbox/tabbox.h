/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

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

#include "tabbox_client_impl.h"
#include "tabbox_config.h"
#include "tabbox_handler_impl.h"
#include "tabbox_logging.h"
#include "tabbox_x11_filter.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/helpers.h"
#include "base/x11/xcb/proto.h"
#include "input/xkb/helpers.h"
#include "kwin_export.h"
#include "kwinglobals.h"
#include "main.h"
#include "win/activation.h"

#include <KGlobalAccel>
#include <KLazyLocalizedString>
#include <QAction>
#include <QKeyEvent>
#include <QKeySequence>
#include <QModelIndex>
#include <QTimer>
#include <X11/keysym.h>
#include <memory>
#include <xcb/xcb_keysyms.h>

class KConfigGroup;
class KLazyLocalizedString;
class QMouseEvent;
class QWheelEvent;

namespace KWin::win
{

class KWIN_EXPORT tabbox_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void tabbox_added(int);
    void tabbox_closed();
    void tabbox_updated();
    void tabbox_key_event(QKeyEvent*);
};

template<typename Space>
class tabbox
{
public:
    using window_t = typename Space::window_t;

    tabbox(Space& space)
        : qobject{std::make_unique<tabbox_qobject>()}
        , space{space}
    {
        m_default_config = tabbox_config();
        m_default_config.set_tabbox_mode(tabbox_config::ClientTabBox);
        m_default_config.set_client_desktop_mode(tabbox_config::OnlyCurrentDesktopClients);
        m_default_config.set_client_applications_mode(tabbox_config::AllWindowsAllApplications);
        m_default_config.set_client_minimized_mode(tabbox_config::IgnoreMinimizedStatus);
        m_default_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        m_default_config.set_client_multi_screen_mode(tabbox_config::IgnoreMultiScreen);
        m_default_config.set_client_switching_mode(tabbox_config::FocusChainSwitching);

        m_alternative_config = tabbox_config();
        m_alternative_config.set_tabbox_mode(tabbox_config::ClientTabBox);
        m_alternative_config.set_client_desktop_mode(tabbox_config::AllDesktopsClients);
        m_alternative_config.set_client_applications_mode(tabbox_config::AllWindowsAllApplications);
        m_alternative_config.set_client_minimized_mode(tabbox_config::IgnoreMinimizedStatus);
        m_alternative_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        m_alternative_config.set_client_multi_screen_mode(tabbox_config::IgnoreMultiScreen);
        m_alternative_config.set_client_switching_mode(tabbox_config::FocusChainSwitching);

        m_default_current_application_config = m_default_config;
        m_default_current_application_config.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);

        m_alternative_current_application_config = m_alternative_config;
        m_alternative_current_application_config.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);

        m_desktop_config = tabbox_config();
        m_desktop_config.set_tabbox_mode(tabbox_config::DesktopTabBox);
        m_desktop_config.set_show_tabbox(true);
        m_desktop_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        m_desktop_config.set_desktop_switching_mode(
            tabbox_config::MostRecentlyUsedDesktopSwitching);

        m_desktop_list_config = tabbox_config();
        m_desktop_list_config.set_tabbox_mode(tabbox_config::DesktopTabBox);
        m_desktop_list_config.set_show_tabbox(true);
        m_desktop_list_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        m_desktop_list_config.set_desktop_switching_mode(tabbox_config::StaticDesktopSwitching);
        m_tabbox = new tabbox_handler_impl(this);
        QTimer::singleShot(0, qobject.get(), [this] { handler_ready(); });

        m_tabbox_mode = TabBoxDesktopMode; // init variables
        QObject::connect(
            &m_delayed_show_timer, &QTimer::timeout, qobject.get(), [this] { show(); });
        QObject::connect(space.qobject.get(),
                         &Space::qobject_t::configChanged,
                         qobject.get(),
                         [this] { reconfigure(); });
    }

    /**
     * Returns the currently displayed client ( only works in TabBoxWindowsMode ).
     * Returns 0 if no client is displayed.
     */
    window_t* current_client()
    {
        if (auto client = static_cast<tabbox_client_impl<window_t>*>(
                m_tabbox->client(m_tabbox->current_index()))) {
            for (auto win : space.windows) {
                if (win == client->client()) {
                    return win;
                }
            }
        }
        return nullptr;
    }

    /**
     * Returns the list of clients potentially displayed ( only works in
     * TabBoxWindowsMode ).
     * Returns an empty list if no clients are available.
     */
    QList<window_t*> current_client_list()
    {
        auto const list = m_tabbox->client_list();
        QList<window_t*> ret;

        for (auto& client_pointer : list) {
            auto client = client_pointer.lock();
            if (!client) {
                continue;
            }
            if (auto c = static_cast<tabbox_client_impl<window_t> const*>(client.get())) {
                ret.append(c->client());
            }
        }
        return ret;
    }

    /**
     * Returns the currently displayed virtual desktop ( only works in
     * TabBoxDesktopListMode )
     * Returns -1 if no desktop is displayed.
     */
    int current_desktop()
    {
        return m_tabbox->desktop(m_tabbox->current_index());
    }

    /**
     * Returns the list of desktops potentially displayed ( only works in
     * TabBoxDesktopListMode )
     * Returns an empty list if no are available.
     */
    QList<int> current_desktop_list()
    {
        return m_tabbox->desktop_list();
    }

    /**
     * Change the currently selected client, and notify the effects.
     *
     * @see setCurrentDesktop
     */
    void set_current_client(window_t* window)
    {
        set_current_index(m_tabbox->index(window->control->tabbox().lock().get()));
    }

    /**
     * Change the currently selected desktop, and notify the effects.
     *
     * @see setCurrentClient
     */
    void set_current_desktop(int new_desktop)
    {
        set_current_index(m_tabbox->desktop_index(new_desktop));
    }

    /**
     * Sets the current mode to \a mode, either TabBoxDesktopListMode or TabBoxWindowsMode
     *
     * @see mode
     */
    void set_mode(TabBoxMode mode)
    {
        m_tabbox_mode = mode;
        switch (mode) {
        case TabBoxWindowsMode:
            m_tabbox->set_config(m_default_config);
            break;
        case TabBoxWindowsAlternativeMode:
            m_tabbox->set_config(m_alternative_config);
            break;
        case TabBoxCurrentAppWindowsMode:
            m_tabbox->set_config(m_default_current_application_config);
            break;
        case TabBoxCurrentAppWindowsAlternativeMode:
            m_tabbox->set_config(m_alternative_current_application_config);
            break;
        case TabBoxDesktopMode:
            m_tabbox->set_config(m_desktop_config);
            break;
        case TabBoxDesktopListMode:
            m_tabbox->set_config(m_desktop_list_config);
            break;
        }
    }

    TabBoxMode mode() const
    {
        return m_tabbox_mode;
    }

    /**
     * Resets the tab box to display the active client in TabBoxWindowsMode, or the
     * current desktop in TabBoxDesktopListMode
     */
    void reset(bool partial_reset = false)
    {
        switch (m_tabbox->config().tabbox_mode()) {
        case tabbox_config::ClientTabBox:
            m_tabbox->create_model(partial_reset);
            if (!partial_reset) {
                if (space.active_client) {
                    set_current_client(space.active_client);
                }

                // it's possible that the active client is not part of the model
                // in that case the index is invalid
                if (!m_tabbox->current_index().isValid())
                    set_current_index(m_tabbox->first());
            } else {
                if (!m_tabbox->current_index().isValid()
                    || !m_tabbox->client(m_tabbox->current_index()))
                    set_current_index(m_tabbox->first());
            }
            break;
        case tabbox_config::DesktopTabBox:
            m_tabbox->create_model();

            if (!partial_reset)
                set_current_desktop(space.virtual_desktop_manager->current());
            break;
        }

        Q_EMIT qobject->tabbox_updated();
    }

    /**
     * Shows the next or previous item, depending on \a next
     */
    void next_prev(bool next = true)
    {
        set_current_index(m_tabbox->next_prev(next), false);
        Q_EMIT qobject->tabbox_updated();
    }

    /**
     * Shows the tab box after some delay.
     *
     * If the 'show_delay' setting is false, show() is simply called.
     *
     * Otherwise, we start a timer for the delay given in the settings and only
     * do a show() when it times out.
     *
     * This means that you can alt-tab between windows and you don't see the
     * tab box immediately. Not only does this make alt-tabbing faster, it gives
     * less 'flicker' to the eyes. You don't need to see the tab box if you're
     * just quickly switching between 2 or 3 windows. It seems to work quite
     * nicely.
     */
    void delayed_show()
    {
        if (is_displayed() || m_delayed_show_timer.isActive())
            // already called show - no need to call it twice
            return;

        if (!m_delay_show_time) {
            show();
            return;
        }

        m_delayed_show_timer.setSingleShot(true);
        m_delayed_show_timer.start(m_delay_show_time);
    }

    /**
     * Notify effects that the tab box is being hidden.
     */
    void hide(bool abort = false)
    {
        m_delayed_show_timer.stop();
        if (m_is_shown) {
            m_is_shown = false;
            unreference();
        }

        Q_EMIT qobject->tabbox_closed();
        if (is_displayed()) {
            qCDebug(KWIN_TABBOX) << "Tab box was not properly closed by an effect";
        }
        m_tabbox->hide(abort);

        if (kwinApp()->x11Connection()) {
            base::x11::xcb::sync();
        }
    }

    /**
     * Increases the reference count, preventing the default tabbox from showing.
     *
     * @see unreference
     * @see is_displayed
     */
    void reference()
    {
        ++m_display_ref_count;
    }

    /**
     * Decreases the reference count. Only when the reference count is 0 will
     * the default tab box be shown.
     */
    void unreference()
    {
        --m_display_ref_count;
    }

    /**
     * Returns whether the tab box is being displayed, either natively or by an
     * effect.
     *
     * @see reference
     * @see unreference
     */
    bool is_displayed() const
    {
        return m_display_ref_count > 0;
    }

    /**
     * @returns @c true if tabbox is shown, @c false if replaced by Effect
     */
    bool is_shown() const
    {
        return m_is_shown;
    }

    bool handle_mouse_event(QMouseEvent* event)
    {
        if (!m_is_shown && is_displayed()) {
            // tabbox has been replaced, check effects
            if (auto& effects = space.render.effects;
                effects && effects->checkInputWindowEvent(event)) {
                return true;
            }
        }
        switch (event->type()) {
        case QEvent::MouseMove:
            if (!m_tabbox->contains_pos(event->globalPos())) {
                // filter out all events which are not on the TabBox window.
                // We don't want windows to react on the mouse events
                return true;
            }
            return false;
        case QEvent::MouseButtonPress:
            if ((!m_is_shown && is_displayed()) || !m_tabbox->contains_pos(event->globalPos())) {
                close(); // click outside closes tab
                return true;
            }
            // fall through
        case QEvent::MouseButtonRelease:
        default:
            // we do not filter it out, the intenal filter takes care
            return false;
        }
        return false;
    }

    bool handle_wheel_event(QWheelEvent* event)
    {
        if (!m_is_shown && is_displayed()) {
            // tabbox has been replaced, check effects
            if (auto& effects = space.render.effects;
                effects && effects->checkInputWindowEvent(event)) {
                return true;
            }
        }
        if (event->angleDelta().y() == 0) {
            return false;
        }
        const QModelIndex index = m_tabbox->next_prev(event->angleDelta().y() > 0);
        if (index.isValid()) {
            set_current_index(index);
        }
        return true;
    }

    void grabbed_key_event(QKeyEvent* event)
    {
        Q_EMIT qobject->tabbox_key_event(event);
        if (!m_is_shown && is_displayed()) {
            // tabbox has been replaced, check effects
            return;
        }
        if (m_no_modifier_grab) {
            if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return
                || event->key() == Qt::Key_Space) {
                accept();
                return;
            }
        }
        m_tabbox->grabbed_key_event(event);
    }

    bool is_grabbed() const
    {
        return m_tab_grab || m_desktop_grab;
    }

    void init_shortcuts()
    {
        key(
            s_windows, [this] { slot_walk_through_windows(); }, Qt::ALT + Qt::Key_Tab);
        key(
            s_windowsRev,
            [this] { slot_walk_back_through_windows(); },
            Qt::ALT + Qt::SHIFT + Qt::Key_Backtab);
        key(
            s_app,
            [this] { slot_walk_through_current_app_windows(); },
            Qt::ALT + Qt::Key_QuoteLeft);
        key(
            s_appRev,
            [this] { slot_walk_back_through_current_app_windows(); },
            Qt::ALT + Qt::Key_AsciiTilde);
        key(s_windowsAlt, [this] { slot_walk_through_windows_alternative(); });
        key(s_windowsAltRev, [this] { slot_walk_back_through_windows_alternative(); });
        key(s_appAlt, [this] { slot_walk_through_current_app_windows_alternative(); });
        key(s_appAltRev, [this] { slot_walk_back_through_current_app_windows_alternative(); });
        key(s_desktops, [this] { slot_walk_through_desktops(); });
        key(s_desktopsRev, [this] { slot_walk_back_through_desktops(); });
        key(s_desktopList, [this] { slot_walk_through_desktop_list(); });
        key(s_desktopListRev, [this] { slot_walk_back_through_desktop_list(); });

        QObject::connect(
            KGlobalAccel::self(),
            &KGlobalAccel::globalShortcutChanged,
            qobject.get(),
            [this](auto action, auto const& seq) { global_shortcut_changed(action, seq); });
    }

    /// Travers all clients according to static order. Useful for CDE-style Alt-tab feature.
    window_t* next_client_static(window_t* c) const
    {
        auto const& list = get_windows_with_control(space.windows);
        if (!c || list.empty()) {
            return nullptr;
        }
        auto pos = index_of(list, c);
        if (pos == -1) {
            return list.front();
        }
        ++pos;
        if (pos == static_cast<int>(list.size())) {
            return list.front();
        }
        return list.at(pos);
    }

    /// Travers all clients according to static order. Useful for CDE-style Alt-tab feature.
    window_t* previous_client_static(window_t* c) const
    {
        auto const& list = get_windows_with_control(space.windows);
        if (!c || list.empty()) {
            return nullptr;
        }

        auto pos = index_of(list, c);
        if (pos == -1) {
            return list.back();
        }
        if (pos == 0) {
            return list.back();
        }
        --pos;
        return list.at(pos);
    }

    int next_desktop_static(int iDesktop) const
    {
        win::virtual_desktop_next functor(*space.virtual_desktop_manager);
        return functor(iDesktop, true);
    }

    int previous_desktop_static(int iDesktop) const
    {
        win::virtual_desktop_previous functor(*space.virtual_desktop_manager);
        return functor(iDesktop, true);
    }

    void key_press(int keyQt)
    {
        enum Direction { Backward = -1, Steady = 0, Forward = 1 };
        Direction direction(Steady);

        auto contains = [](const QKeySequence& shortcut, int key) -> bool {
            for (int i = 0; i < shortcut.count(); ++i) {
                if (shortcut[i] == key) {
                    return true;
                }
            }
            return false;
        };

        // tests whether a shortcut matches and handles pitfalls on ShiftKey invocation
        auto direction_for = [keyQt, contains](const QKeySequence& forward,
                                               const QKeySequence& backward) -> Direction {
            if (contains(forward, keyQt))
                return Forward;
            if (contains(backward, keyQt))
                return Backward;
            if (!(keyQt & Qt::ShiftModifier))
                return Steady;

            // Before testing the unshifted key (Ctrl+A vs. Ctrl+Shift+a etc.), see whether this is
            // +Shift+Tab and check that against +Shift+Backtab (as well)
            Qt::KeyboardModifiers mods = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier
                | Qt::MetaModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier;
            mods &= keyQt;
            if ((keyQt & ~mods) == Qt::Key_Tab) {
                if (contains(forward, mods | Qt::Key_Backtab))
                    return Forward;
                if (contains(backward, mods | Qt::Key_Backtab))
                    return Backward;
            }

            // if the shortcuts do not match, try matching again after filtering the shift key from
            // keyQt it is needed to handle correctly the ALT+~ shorcut for example as it is coded
            // as ALT+SHIFT+~ in keyQt
            if (contains(forward, keyQt & ~Qt::ShiftModifier))
                return Forward;
            if (contains(backward, keyQt & ~Qt::ShiftModifier))
                return Backward;

            return Steady;
        };

        if (m_tab_grab) {
            static const int mode_count = 4;
            static const TabBoxMode modes[mode_count] = {TabBoxWindowsMode,
                                                         TabBoxWindowsAlternativeMode,
                                                         TabBoxCurrentAppWindowsMode,
                                                         TabBoxCurrentAppWindowsAlternativeMode};
            const QKeySequence cuts[2 * mode_count]
                = {// forward
                   m_cut_walk_through_windows,
                   m_cut_walk_through_windows_alternative,
                   m_cut_walk_through_current_app_windows,
                   m_cut_walk_through_current_app_windows_alternative,
                   // backward
                   m_cut_walk_through_windows_reverse,
                   m_cut_walk_through_windows_alternative_reverse,
                   m_cut_walk_through_current_app_windows_reverse,
                   m_cut_walk_through_current_app_windows_alternative_reverse};
            bool tested_current = false; // in case of collision, prefer to stay in the current mode
            int i = 0, j = 0;
            while (true) {
                if (!tested_current && modes[i] != mode()) {
                    ++j;
                    i = (i + 1) % mode_count;
                    continue;
                }
                if (tested_current && modes[i] == mode()) {
                    break;
                }
                tested_current = true;
                direction = direction_for(cuts[i], cuts[i + mode_count]);
                if (direction != Steady) {
                    if (modes[i] != mode()) {
                        accept(false);
                        set_mode(modes[i]);
                        auto replayWithChangedTabboxMode = [this, direction]() {
                            reset();
                            next_prev(direction == Forward);
                        };
                        QTimer::singleShot(50, qobject.get(), replayWithChangedTabboxMode);
                    }
                    break;
                } else if (++j > 2 * mode_count) { // guarding counter for invalid modes
                    qCDebug(KWIN_TABBOX) << "Invalid TabBoxMode";
                    return;
                }
                i = (i + 1) % mode_count;
            }
            if (direction != Steady) {
                qCDebug(KWIN_TABBOX)
                    << "== " << cuts[i].toString() << " or " << cuts[i + mode_count].toString();
                kde_walk_through_windows(direction == Forward);
            }
        } else if (m_desktop_grab) {
            direction
                = direction_for(m_cut_walk_through_desktops, m_cut_walk_through_desktops_reverse);
            if (direction == Steady)
                direction = direction_for(m_cut_walk_through_desktop_list,
                                          m_cut_walk_through_desktop_list_reverse);
            if (direction != Steady)
                walk_through_desktops(direction == Forward);
        }

        if (m_desktop_grab || m_tab_grab) {
            if (((keyQt & ~Qt::KeyboardModifierMask) == Qt::Key_Escape) && direction == Steady) {
                // if Escape is part of the shortcut, don't cancel
                close(true);
            } else if (direction == Steady) {
                QKeyEvent event(
                    QEvent::KeyPress, keyQt & ~Qt::KeyboardModifierMask, Qt::NoModifier);
                grabbed_key_event(&event);
            }
        }
    }

    void modifiers_released()
    {
        if (m_no_modifier_grab) {
            return;
        }
        if (m_tab_grab) {
            bool old_control_grab = m_desktop_grab;
            accept();
            m_desktop_grab = old_control_grab;
        }
        if (m_desktop_grab) {
            bool old_tab_grab = m_tab_grab;
            int desktop = current_desktop();
            close();
            m_tab_grab = old_tab_grab;
            if (desktop != -1) {
                set_current_desktop(desktop);
                space.virtual_desktop_manager->setCurrent(desktop);
            }
        }
    }

    bool forced_global_mouse_grab() const
    {
        return m_forced_global_mouse_grab;
    }

    bool no_modifier_grab() const
    {
        return m_no_modifier_grab;
    }

    void set_current_index(QModelIndex index, bool notify_effects = true)
    {
        if (!index.isValid())
            return;
        m_tabbox->set_current_index(index);
        if (notify_effects) {
            Q_EMIT qobject->tabbox_updated();
        }
    }

    /**
     * Notify effects that the tab box is being shown, and only display the
     * default tabbox QFrame if no effect has referenced the tabbox.
     */
    void show()
    {
        Q_EMIT qobject->tabbox_added(m_tabbox_mode);
        if (is_displayed()) {
            m_is_shown = false;
            return;
        }

        set_showing_desktop(space, false);
        reference();
        m_is_shown = true;
        m_tabbox->show();
    }

    void close(bool abort = false)
    {
        if (is_grabbed()) {
            remove_tabbox_grab();
        }
        hide(abort);
        space.input->get_pointer()->setEnableConstraints(true);
        m_tab_grab = false;
        m_desktop_grab = false;
        m_no_modifier_grab = false;
    }

    void accept(bool close_tabbox = true)
    {
        auto c = current_client();
        if (close_tabbox)
            close();
        if (c) {
            activate_window(space, c);
            if (win::is_desktop(c))
                set_showing_desktop(space, !space.showing_desktop);
        }
    }

    void slot_walk_through_desktops()
    {
        if (!m_ready || is_grabbed()) {
            return;
        }
        if (areModKeysDepressed(space.input->platform, m_cut_walk_through_desktops)) {
            if (start_walk_through_desktops())
                walk_through_desktops(true);
        } else {
            one_step_through_desktops(true);
        }
    }

    void slot_walk_back_through_desktops()
    {
        if (!m_ready || is_grabbed()) {
            return;
        }
        if (areModKeysDepressed(space.input->platform, m_cut_walk_through_desktops_reverse)) {
            if (start_walk_through_desktops())
                walk_through_desktops(false);
        } else {
            one_step_through_desktops(false);
        }
    }

    void slot_walk_through_desktop_list()
    {
        if (!m_ready || is_grabbed()) {
            return;
        }
        if (areModKeysDepressed(space.input->platform, m_cut_walk_through_desktop_list)) {
            if (start_walk_through_desktop_list())
                walk_through_desktops(true);
        } else {
            one_step_through_desktop_list(true);
        }
    }

    void slot_walk_back_through_desktop_list()
    {
        if (!m_ready || is_grabbed()) {
            return;
        }
        if (areModKeysDepressed(space.input->platform, m_cut_walk_through_desktop_list_reverse)) {
            if (start_walk_through_desktop_list())
                walk_through_desktops(false);
        } else {
            one_step_through_desktop_list(false);
        }
    }

    void slot_walk_through_windows()
    {
        navigating_through_windows(true, m_cut_walk_through_windows, TabBoxWindowsMode);
    }

    void slot_walk_back_through_windows()
    {
        navigating_through_windows(false, m_cut_walk_through_windows_reverse, TabBoxWindowsMode);
    }

    void slot_walk_through_windows_alternative()
    {
        navigating_through_windows(
            true, m_cut_walk_through_windows_alternative, TabBoxWindowsAlternativeMode);
    }

    void slot_walk_back_through_windows_alternative()
    {
        navigating_through_windows(
            false, m_cut_walk_through_windows_alternative_reverse, TabBoxWindowsAlternativeMode);
    }

    void slot_walk_through_current_app_windows()
    {
        navigating_through_windows(
            true, m_cut_walk_through_current_app_windows, TabBoxCurrentAppWindowsMode);
    }

    void slot_walk_back_through_current_app_windows()
    {
        navigating_through_windows(
            false, m_cut_walk_through_current_app_windows_reverse, TabBoxCurrentAppWindowsMode);
    }

    void slot_walk_through_current_app_windows_alternative()
    {
        navigating_through_windows(true,
                                   m_cut_walk_through_current_app_windows_alternative,
                                   TabBoxCurrentAppWindowsAlternativeMode);
    }

    void slot_walk_back_through_current_app_windows_alternative()
    {
        navigating_through_windows(false,
                                   m_cut_walk_through_current_app_windows_alternative_reverse,
                                   TabBoxCurrentAppWindowsAlternativeMode);
    }

    void handler_ready()
    {
        m_tabbox->set_config(m_default_config);
        reconfigure();
        m_ready = true;
    }

    bool toggle(ElectricBorder eb)
    {
        if (border_activate_alternative.find(eb) != border_activate_alternative.end()) {
            return toggle_mode(TabBoxWindowsAlternativeMode);
        } else {
            return toggle_mode(TabBoxWindowsMode);
        }
    }

    std::unique_ptr<tabbox_qobject> qobject;
    Space& space;

private:
    /**
     * Handles alt-tab / control-tab
     */
    static bool areKeySymXsDepressed(const uint keySyms[], int nKeySyms)
    {
        struct KeySymbolsDeleter {
            static inline void cleanup(xcb_key_symbols_t* symbols)
            {
                xcb_key_symbols_free(symbols);
            }
        };

        base::x11::xcb::query_keymap keys;

        QScopedPointer<xcb_key_symbols_t, KeySymbolsDeleter> symbols(
            xcb_key_symbols_alloc(connection()));
        if (symbols.isNull() || !keys) {
            return false;
        }
        const auto keymap = keys->keys;

        bool depressed = false;
        for (int iKeySym = 0; iKeySym < nKeySyms; iKeySym++) {
            uint keySymX = keySyms[iKeySym];
            xcb_keycode_t* keyCodes = xcb_key_symbols_get_keycode(symbols.data(), keySymX);
            if (!keyCodes) {
                continue;
            }

            int j = 0;
            while (keyCodes[j] != XCB_NO_SYMBOL) {
                const xcb_keycode_t keyCodeX = keyCodes[j++];
                int i = keyCodeX / 8;
                char mask = 1 << (keyCodeX - (i * 8));

                if (i < 0 || i >= 32) {
                    continue;
                }

                qCDebug(KWIN_TABBOX) << iKeySym << ": keySymX=0x" << QString::number(keySymX, 16)
                                     << " i=" << i << " mask=0x" << QString::number(mask, 16)
                                     << " keymap[i]=0x" << QString::number(keymap[i], 16);

                if (keymap[i] & mask) {
                    depressed = true;
                    break;
                }
            }

            free(keyCodes);
        }

        return depressed;
    }

    static bool areModKeysDepressedX11(const QKeySequence& seq)
    {
        uint rgKeySyms[10];
        int nKeySyms = 0;
        int mod = seq[seq.count() - 1] & Qt::KeyboardModifierMask;

        if (mod & Qt::SHIFT) {
            rgKeySyms[nKeySyms++] = XK_Shift_L;
            rgKeySyms[nKeySyms++] = XK_Shift_R;
        }
        if (mod & Qt::CTRL) {
            rgKeySyms[nKeySyms++] = XK_Control_L;
            rgKeySyms[nKeySyms++] = XK_Control_R;
        }
        if (mod & Qt::ALT) {
            rgKeySyms[nKeySyms++] = XK_Alt_L;
            rgKeySyms[nKeySyms++] = XK_Alt_R;
        }
        if (mod & Qt::META) {
            // It would take some code to determine whether the Win key
            // is associated with Super or Meta, so check for both.
            // See bug #140023 for details.
            rgKeySyms[nKeySyms++] = XK_Super_L;
            rgKeySyms[nKeySyms++] = XK_Super_R;
            rgKeySyms[nKeySyms++] = XK_Meta_L;
            rgKeySyms[nKeySyms++] = XK_Meta_R;
        }

        return areKeySymXsDepressed(rgKeySyms, nKeySyms);
    }

    template<typename Input>
    static bool areModKeysDepressedWayland(Input const& input, const QKeySequence& seq)
    {
        const int mod = seq[seq.count() - 1] & Qt::KeyboardModifierMask;
        auto const mods
            = input::xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(input);
        if ((mod & Qt::SHIFT) && mods.testFlag(Qt::ShiftModifier)) {
            return true;
        }
        if ((mod & Qt::CTRL) && mods.testFlag(Qt::ControlModifier)) {
            return true;
        }
        if ((mod & Qt::ALT) && mods.testFlag(Qt::AltModifier)) {
            return true;
        }
        if ((mod & Qt::META) && mods.testFlag(Qt::MetaModifier)) {
            return true;
        }
        return false;
    }

    template<typename Input>
    static bool areModKeysDepressed(Input const& input, const QKeySequence& seq)
    {
        if (seq.isEmpty())
            return false;
        if (kwinApp()->shouldUseWaylandForCompositing()) {
            return areModKeysDepressedWayland(input, seq);
        } else {
            return areModKeysDepressedX11(seq);
        }
    }

    static std::vector<window_t*> get_windows_with_control(std::vector<window_t*>& windows)
    {
        std::vector<window_t*> with_control;
        for (auto win : windows) {
            if (win->control) {
                with_control.push_back(win);
            }
        }
        return with_control;
    }

    void load_config(const KConfigGroup& config, win::tabbox_config& tabbox_config)
    {
        tabbox_config.set_client_desktop_mode(tabbox_config::ClientDesktopMode(
            config.readEntry<int>("DesktopMode", tabbox_config::default_desktop_mode())));
        tabbox_config.set_client_applications_mode(tabbox_config::ClientApplicationsMode(
            config.readEntry<int>("ApplicationsMode", tabbox_config::default_applications_mode())));
        tabbox_config.set_client_minimized_mode(tabbox_config::ClientMinimizedMode(
            config.readEntry<int>("MinimizedMode", tabbox_config::default_minimized_mode())));
        tabbox_config.set_show_desktop_mode(tabbox_config::ShowDesktopMode(
            config.readEntry<int>("ShowDesktopMode", tabbox_config::default_show_desktop_mode())));
        tabbox_config.set_client_multi_screen_mode(tabbox_config::ClientMultiScreenMode(
            config.readEntry<int>("MultiScreenMode", tabbox_config::default_multi_screen_mode())));
        tabbox_config.set_client_switching_mode(tabbox_config::ClientSwitchingMode(
            config.readEntry<int>("SwitchingMode", tabbox_config::default_switching_mode())));

        tabbox_config.set_show_tabbox(
            config.readEntry<bool>("ShowTabBox", tabbox_config::default_show_tabbox()));
        tabbox_config.set_highlight_windows(
            config.readEntry<bool>("HighlightWindows", tabbox_config::default_highlight_window()));

        tabbox_config.set_layout_name(
            config.readEntry<QString>("LayoutName", tabbox_config::default_layout_name()));
    }

    // TabBoxWindowsMode | TabBoxWindowsAlternativeMode
    bool start_kde_walk_through_windows(TabBoxMode mode)
    {
        if (!establish_tabbox_grab()) {
            return false;
        }
        m_tab_grab = true;
        m_no_modifier_grab = false;
        set_mode(mode);
        reset();
        return true;
    }

    // TabBoxDesktopMode | TabBoxDesktopListMode
    bool start_walk_through_desktops(TabBoxMode mode)
    {
        if (!establish_tabbox_grab()) {
            return false;
        }
        m_desktop_grab = true;
        m_no_modifier_grab = false;
        set_mode(mode);
        reset();
        return true;
    }

    bool start_walk_through_desktops()
    {
        return start_walk_through_desktops(TabBoxDesktopMode);
    }

    bool start_walk_through_desktop_list()
    {
        return start_walk_through_desktops(TabBoxDesktopListMode);
    }

    // TabBoxWindowsMode | TabBoxWindowsAlternativeMode
    void navigating_through_windows(bool forward, const QKeySequence& shortcut, TabBoxMode mode)
    {
        if (!m_ready || is_grabbed()) {
            return;
        }
        if (!kwinApp()->options->qobject->focusPolicyIsReasonable()) {
            // ungrabXKeyboard(); // need that because of accelerator raw mode
            //  CDE style raise / lower
            cde_walk_through_windows(forward);
        } else {
            if (areModKeysDepressed(space.input->platform, shortcut)) {
                if (start_kde_walk_through_windows(mode))
                    kde_walk_through_windows(forward);
            } else
                // if the shortcut has no modifiers, don't show the tabbox,
                // don't grab, but simply go to the next window
                kde_one_step_through_windows(forward, mode);
        }
    }

    void kde_walk_through_windows(bool forward)
    {
        next_prev(forward);
        delayed_show();
    }

    void cde_walk_through_windows(bool forward)
    {
        window_t* c = nullptr;
        // this function find the first suitable client for unreasonable focus
        // policies - the topmost one, with some exceptions (can't be keepabove/below,
        // otherwise it gets stuck on them)
        //     Q_ASSERT(space.block_stacking_updates == 0);
        for (int i = space.stacking_order->stack.size() - 1; i >= 0; --i) {
            auto window = space.stacking_order->stack.at(i);
            if (window->control && window->isOnCurrentDesktop() && !win::is_special_window(window)
                && window->isShown() && win::wants_tab_focus(window) && !window->control->keep_above
                && !window->control->keep_below) {
                c = window;
                break;
            }
        }
        auto nc = c;
        bool options_traverse_all;
        {
            KConfigGroup group(kwinApp()->config(), "TabBox");
            options_traverse_all = group.readEntry("TraverseAll", false);
        }

        window_t* first_client = nullptr;
        do {
            nc = forward ? next_client_static(nc) : previous_client_static(nc);
            if (!first_client) {
                // When we see our first client for the second time,
                // it's time to stop.
                first_client = nc;
            } else if (nc == first_client) {
                // No candidates found.
                nc = nullptr;
                break;
            }
        } while (nc && nc != c
                 && ((!options_traverse_all && !nc->isOnDesktop(current_desktop()))
                     || nc->control->minimized || !win::wants_tab_focus(nc)
                     || nc->control->keep_above || nc->control->keep_below));
        if (nc) {
            if (c && c != nc)
                win::lower_window(&space, c);
            if (kwinApp()->options->qobject->focusPolicyIsReasonable()) {
                activate_window(space, nc);
            } else {
                if (!nc->isOnDesktop(current_desktop()))
                    set_current_desktop(nc->desktop());
                win::raise_window(&space, nc);
            }
        }
    }

    void walk_through_desktops(bool forward)
    {
        next_prev(forward);
        delayed_show();
    }

    // TabBoxWindowsMode | TabBoxWindowsAlternativeMode
    void kde_one_step_through_windows(bool forward, TabBoxMode mode)
    {
        set_mode(mode);
        reset();
        next_prev(forward);
        if (auto c = current_client()) {
            activate_window(space, c);
        }
    }

    // TabBoxDesktopMode | TabBoxDesktopListMode
    void one_step_through_desktops(bool forward, TabBoxMode mode)
    {
        set_mode(mode);
        reset();
        next_prev(forward);
        if (current_desktop() != -1)
            set_current_desktop(current_desktop());
    }

    void one_step_through_desktops(bool forward)
    {
        one_step_through_desktops(forward, TabBoxDesktopMode);
    }

    void one_step_through_desktop_list(bool forward)
    {
        one_step_through_desktops(forward, TabBoxDesktopListMode);
    }

    bool establish_tabbox_grab()
    {
        if (kwinApp()->shouldUseWaylandForCompositing()) {
            m_forced_global_mouse_grab = true;
            return true;
        }
        kwinApp()->update_x11_time_from_clock();
        if (!base::x11::grab_keyboard())
            return false;
        // Don't try to establish a global mouse grab using XGrabPointer, as that would prevent
        // using Alt+Tab while DND (#44972). However force passive grabs on all windows
        // in order to catch MouseRelease events and close the tabbox (#67416).
        // All clients already have passive grabs in their wrapper windows, so check only
        // the active client, which may not have it.
        Q_ASSERT(!m_forced_global_mouse_grab);
        m_forced_global_mouse_grab = true;
        if (space.active_client) {
            space.active_client->control->update_mouse_grab();
        }
        m_x11_event_filter.reset(new tabbox_x11_filter<tabbox<Space>>(*this));
        return true;
    }

    void remove_tabbox_grab()
    {
        if (kwinApp()->shouldUseWaylandForCompositing()) {
            m_forced_global_mouse_grab = false;
            return;
        }
        kwinApp()->update_x11_time_from_clock();
        base::x11::ungrab_keyboard();
        Q_ASSERT(m_forced_global_mouse_grab);
        m_forced_global_mouse_grab = false;
        if (space.active_client) {
            space.active_client->control->update_mouse_grab();
        }
        m_x11_event_filter.reset();
    }

    template<typename Slot>
    void key(const KLazyLocalizedString& action_name,
             Slot slot,
             const QKeySequence& shortcut = QKeySequence())
    {
        auto a = new QAction(qobject.get());
        a->setProperty("componentName", QStringLiteral(KWIN_NAME));
        a->setObjectName(QString::fromUtf8(action_name.untranslatedText()));
        a->setText(action_name.toString());
        KGlobalAccel::self()->setGlobalShortcut(a, QList<QKeySequence>() << shortcut);
        space.input->platform.registerShortcut(shortcut, a, qobject.get(), slot);
        auto cuts = KGlobalAccel::self()->shortcut(a);
        global_shortcut_changed(a, cuts.isEmpty() ? QKeySequence() : cuts.first());
    }

    bool toggle_mode(TabBoxMode mode)
    {
        if (!kwinApp()->options->qobject->focusPolicyIsReasonable()) {
            // not supported.
            return false;
        }
        if (is_displayed()) {
            accept();
            return true;
        }
        if (!establish_tabbox_grab()) {
            return false;
        }
        m_no_modifier_grab = m_tab_grab = true;
        set_mode(mode);
        reset();
        show();
        return true;
    }

    void reconfigure()
    {
        KSharedConfigPtr c = kwinApp()->config();
        KConfigGroup config = c->group("TabBox");

        load_config(c->group("TabBox"), m_default_config);
        load_config(c->group("TabBoxAlternative"), m_alternative_config);

        m_default_current_application_config = m_default_config;
        m_default_current_application_config.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);
        m_alternative_current_application_config = m_alternative_config;
        m_alternative_current_application_config.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);

        m_tabbox->set_config(m_default_config);

        m_delay_show = config.readEntry<bool>("ShowDelay", true);
        m_delay_show_time = config.readEntry<int>("DelayTime", 90);

        const QString default_desktop_layout = QStringLiteral("org.kde.breeze.desktop");
        m_desktop_config.set_layout_name(config.readEntry("DesktopLayout", default_desktop_layout));
        m_desktop_list_config.set_layout_name(
            config.readEntry("DesktopListLayout", default_desktop_layout));

        auto recreate_borders = [this, &config](auto& borders, auto const& border_config) {
            for (auto const& [border, id] : borders) {
                space.edges->unreserve(border, id);
            }

            borders.clear();
            QStringList list = config.readEntry(border_config, QStringList());

            for (auto const& s : qAsConst(list)) {
                bool ok;
                auto i = s.toInt(&ok);
                if (!ok) {
                    continue;
                }
                auto border = static_cast<ElectricBorder>(i);
                auto id = space.edges->reserve(border, [this](auto eb) { return toggle(eb); });
                borders.insert({border, id});
            }
        };

        recreate_borders(border_activate, QStringLiteral("BorderActivate"));
        recreate_borders(border_activate_alternative, QStringLiteral("BorderAlternativeActivate"));

        auto touch_config = [this, config](const QString& key,
                                           QHash<ElectricBorder, QAction*>& actions,
                                           TabBoxMode mode,
                                           const QStringList& defaults = QStringList{}) {
            // fist erase old config
            for (auto it = actions.begin(); it != actions.end();) {
                delete it.value();
                it = actions.erase(it);
            }
            // now new config
            const QStringList list = config.readEntry(key, defaults);
            for (const auto& s : list) {
                bool ok;
                const int i = s.toInt(&ok);
                if (!ok) {
                    continue;
                }
                auto a = new QAction(qobject.get());
                QObject::connect(
                    a, &QAction::triggered, qobject.get(), [this, mode] { toggle_mode(mode); });
                space.edges->reserveTouch(ElectricBorder(i), a);
                actions.insert(ElectricBorder(i), a);
            }
        };
        touch_config(QStringLiteral("TouchBorderActivate"), m_touch_activate, TabBoxWindowsMode);
        touch_config(QStringLiteral("TouchBorderAlternativeActivate"),
                     m_touch_alternative_activate,
                     TabBoxWindowsAlternativeMode);
    }

    void global_shortcut_changed(QAction* action, const QKeySequence& seq)
    {
        if (qstrcmp(qPrintable(action->objectName()), s_windows.untranslatedText()) == 0) {
            m_cut_walk_through_windows = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_windowsRev.untranslatedText())
                   == 0) {
            m_cut_walk_through_windows_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_app.untranslatedText()) == 0) {
            m_cut_walk_through_current_app_windows = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_appRev.untranslatedText()) == 0) {
            m_cut_walk_through_current_app_windows_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAlt.untranslatedText())
                   == 0) {
            m_cut_walk_through_windows_alternative = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAltRev.untranslatedText())
                   == 0) {
            m_cut_walk_through_windows_alternative_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_appAlt.untranslatedText()) == 0) {
            m_cut_walk_through_current_app_windows_alternative = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_appAltRev.untranslatedText()) == 0) {
            m_cut_walk_through_current_app_windows_alternative_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktops.untranslatedText()) == 0) {
            m_cut_walk_through_desktops = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktopsRev.untranslatedText())
                   == 0) {
            m_cut_walk_through_desktops_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktopList.untranslatedText())
                   == 0) {
            m_cut_walk_through_desktop_list = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktopListRev.untranslatedText())
                   == 0) {
            m_cut_walk_through_desktop_list_reverse = seq;
        }
    }

    TabBoxMode m_tabbox_mode;
    tabbox_handler_impl<tabbox<Space>>* m_tabbox;
    bool m_delay_show;
    int m_delay_show_time;

    QTimer m_delayed_show_timer;
    int m_display_ref_count{0};

    tabbox_config m_default_config;
    tabbox_config m_alternative_config;
    tabbox_config m_default_current_application_config;
    tabbox_config m_alternative_current_application_config;
    tabbox_config m_desktop_config;
    tabbox_config m_desktop_list_config;

    // false if an effect has referenced the tabbox
    // true if tabbox is active (independent on showTabbox setting)
    bool m_is_shown{false};
    bool m_desktop_grab{false};
    bool m_tab_grab{false};

    QKeySequence m_cut_walk_through_desktops, m_cut_walk_through_desktops_reverse;
    QKeySequence m_cut_walk_through_desktop_list, m_cut_walk_through_desktop_list_reverse;
    QKeySequence m_cut_walk_through_windows, m_cut_walk_through_windows_reverse;
    QKeySequence m_cut_walk_through_windows_alternative,
        m_cut_walk_through_windows_alternative_reverse;
    QKeySequence m_cut_walk_through_current_app_windows,
        m_cut_walk_through_current_app_windows_reverse;
    QKeySequence m_cut_walk_through_current_app_windows_alternative,
        m_cut_walk_through_current_app_windows_alternative_reverse;

    // true if tabbox is in modal mode which does not require holding a modifier
    bool m_no_modifier_grab{false};
    bool m_forced_global_mouse_grab{false};

    // indicates whether the config is completely loaded
    bool m_ready{false};

    std::unordered_map<ElectricBorder, uint32_t> border_activate;
    std::unordered_map<ElectricBorder, uint32_t> border_activate_alternative;

    QHash<ElectricBorder, QAction*> m_touch_activate;
    QHash<ElectricBorder, QAction*> m_touch_alternative_activate;
    QScopedPointer<base::x11::event_filter> m_x11_event_filter;

    static constexpr auto s_windows{kli18n("Walk Through Windows")};
    static constexpr auto s_windowsRev{kli18n("Walk Through Windows (Reverse)")};
    static constexpr auto s_windowsAlt{kli18n("Walk Through Windows Alternative")};
    static constexpr auto s_windowsAltRev{kli18n("Walk Through Windows Alternative (Reverse)")};
    static constexpr auto s_app{kli18n("Walk Through Windows of Current Application")};
    static constexpr auto s_appRev{kli18n("Walk Through Windows of Current Application (Reverse)")};
    static constexpr auto s_appAlt{
        kli18n("Walk Through Windows of Current Application Alternative")};
    static constexpr auto s_appAltRev{
        kli18n("Walk Through Windows of Current Application Alternative (Reverse)")};
    static constexpr auto s_desktops{kli18n("Walk Through Desktops")};
    static constexpr auto s_desktopsRev{kli18n("Walk Through Desktops (Reverse)")};
    static constexpr auto s_desktopList{kli18n("Walk Through Desktop List")};
    static constexpr auto s_desktopListRev{kli18n("Walk Through Desktop List (Reverse)")};
};

}
