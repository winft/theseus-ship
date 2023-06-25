/*
SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "tabbox_client_impl.h"
#include "tabbox_config.h"
#include "tabbox_handler_impl.h"
#include "tabbox_logging.h"
#include "tabbox_x11_filter.h"

#include <base/x11/grabs.h>
#include <base/x11/xcb/helpers.h>
#include <base/x11/xcb/proto.h>
#include <config-kwin.h>
#include <kwin_export.h>
#include <win/activation.h>

#include <KLazyLocalizedString>
#include <QAction>
#include <QKeyEvent>
#include <QKeySequence>
#include <QModelIndex>
#include <QTimer>
#include <memory>

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
    void tabbox_added(tabbox_mode);
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
        config.normal = tabbox_config();
        config.normal.set_tabbox_mode(tabbox_config::ClientTabBox);
        config.normal.set_client_desktop_mode(tabbox_config::OnlyCurrentDesktopClients);
        config.normal.set_client_applications_mode(tabbox_config::AllWindowsAllApplications);
        config.normal.set_client_minimized_mode(tabbox_config::IgnoreMinimizedStatus);
        config.normal.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        config.normal.set_client_multi_screen_mode(tabbox_config::IgnoreMultiScreen);
        config.normal.set_client_switching_mode(tabbox_config::FocusChainSwitching);

        config.alternative = tabbox_config();
        config.alternative.set_tabbox_mode(tabbox_config::ClientTabBox);
        config.alternative.set_client_desktop_mode(tabbox_config::AllDesktopsClients);
        config.alternative.set_client_applications_mode(tabbox_config::AllWindowsAllApplications);
        config.alternative.set_client_minimized_mode(tabbox_config::IgnoreMinimizedStatus);
        config.alternative.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        config.alternative.set_client_multi_screen_mode(tabbox_config::IgnoreMultiScreen);
        config.alternative.set_client_switching_mode(tabbox_config::FocusChainSwitching);

        config.normal_current_app = config.normal;
        config.normal_current_app.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);

        config.alternative_current_app = config.alternative;
        config.alternative_current_app.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);

        config.desktop = tabbox_config();
        config.desktop.set_tabbox_mode(tabbox_config::DesktopTabBox);
        config.desktop.set_show_tabbox(true);
        config.desktop.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        config.desktop.set_desktop_switching_mode(tabbox_config::MostRecentlyUsedDesktopSwitching);

        config.desktop_list = tabbox_config();
        config.desktop_list.set_tabbox_mode(tabbox_config::DesktopTabBox);
        config.desktop_list.set_show_tabbox(true);
        config.desktop_list.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
        config.desktop_list.set_desktop_switching_mode(tabbox_config::StaticDesktopSwitching);
        handler = new tabbox_handler_impl(this);
        QTimer::singleShot(0, qobject.get(), [this] { set_handler_ready(); });

        current_mode = tabbox_mode::desktop; // init variables
        QObject::connect(
            &delay_show_data.timer, &QTimer::timeout, qobject.get(), [this] { show(); });
        QObject::connect(space.qobject.get(),
                         &Space::qobject_t::configChanged,
                         qobject.get(),
                         [this] { reconfigure(); });
    }

    /**
     * Returns the currently displayed client ( only works in TabBoxWindowsMode ).
     * Returns 0 if no client is displayed.
     */
    std::optional<window_t> current_client()
    {
        if (auto client = static_cast<tabbox_client_impl<window_t>*>(
                handler->client(handler->current_index()))) {
            for (auto win : space.windows) {
                if (win == client->client()) {
                    return win;
                }
            }
        }
        return {};
    }

    /**
     * Returns the list of clients potentially displayed ( only works in
     * TabBoxWindowsMode ).
     * Returns an empty list if no clients are available.
     */
    QList<window_t> current_client_list()
    {
        QList<window_t> ret;
        for (auto&& win : handler->client_list()) {
            ret.append(static_cast<tabbox_client_impl<window_t> const*>(win)->client());
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
        return handler->desktop(handler->current_index());
    }

    /**
     * Returns the list of desktops potentially displayed ( only works in
     * TabBoxDesktopListMode )
     * Returns an empty list if no are available.
     */
    QList<int> current_desktop_list()
    {
        return handler->desktop_list();
    }

    /**
     * Change the currently selected client, and notify the effects.
     *
     * @see setCurrentDesktop
     */
    void set_current_client(window_t window)
    {
        auto client = std::visit(
            overload{[](auto&& win) -> tabbox_client* { return win->control->tabbox.get(); }},
            window);
        set_current_index(handler->index(client));
    }

    /**
     * Change the currently selected desktop, and notify the effects.
     *
     * @see setCurrentClient
     */
    void set_current_desktop(int new_desktop)
    {
        set_current_index(handler->desktop_index(new_desktop));
    }

    /**
     * Sets the current mode to \a mode, either TabBoxDesktopListMode or TabBoxWindowsMode
     *
     * @see mode
     */
    void set_mode(tabbox_mode mode)
    {
        current_mode = mode;
        switch (mode) {
        case tabbox_mode::windows:
            handler->set_config(config.normal);
            break;
        case tabbox_mode::windows_alternative:
            handler->set_config(config.alternative);
            break;
        case tabbox_mode::current_app_windows:
            handler->set_config(config.normal_current_app);
            break;
        case tabbox_mode::current_app_windows_alternative:
            handler->set_config(config.alternative_current_app);
            break;
        case tabbox_mode::desktop:
            handler->set_config(config.desktop);
            break;
        case tabbox_mode::desktop_list:
            handler->set_config(config.desktop_list);
            break;
        }
    }

    tabbox_mode mode() const
    {
        return current_mode;
    }

    /**
     * Resets the tab box to display the active client in TabBoxWindowsMode, or the
     * current desktop in TabBoxDesktopListMode
     */
    void reset(bool partial_reset = false)
    {
        switch (handler->config().tabbox_mode()) {
        case tabbox_config::ClientTabBox:
            handler->create_model(partial_reset);

            if (partial_reset) {
                if (!handler->current_index().isValid()
                    || !handler->client(handler->current_index())) {
                    set_current_index(handler->first());
                }
            } else {
                if (space.stacking.active) {
                    set_current_client(*space.stacking.active);
                }

                // it's possible that the active client is not part of the model
                // in that case the index is invalid
                if (!handler->current_index().isValid()) {
                    set_current_index(handler->first());
                }
            }

            break;
        case tabbox_config::DesktopTabBox:
            handler->create_model();

            if (!partial_reset) {
                set_current_desktop(space.virtual_desktop_manager->current());
            }
            break;
        }

        Q_EMIT qobject->tabbox_updated();
    }

    /**
     * Shows the next or previous item, depending on \a next
     */
    void next_prev(bool next = true)
    {
        set_current_index(handler->next_prev(next), false);
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
        if (is_displayed() || delay_show_data.timer.isActive())
            // already called show - no need to call it twice
            return;

        if (!delay_show_data.duration) {
            show();
            return;
        }

        delay_show_data.timer.setSingleShot(true);
        delay_show_data.timer.start(delay_show_data.duration);
    }

    /**
     * Notify effects that the tab box is being hidden.
     */
    void hide(bool abort = false)
    {
        delay_show_data.timer.stop();
        if (is_natively_shown) {
            is_natively_shown = false;
            unreference();
        }

        Q_EMIT qobject->tabbox_closed();
        if (is_displayed()) {
            qCDebug(KWIN_TABBOX) << "Tab box was not properly closed by an effect";
        }
        handler->hide(abort);
    }

    /**
     * Increases the reference count, preventing the default tabbox from showing.
     *
     * @see unreference
     * @see is_displayed
     */
    void reference()
    {
        ++displayed_ref_count;
    }

    /**
     * Decreases the reference count. Only when the reference count is 0 will
     * the default tab box be shown.
     */
    void unreference()
    {
        --displayed_ref_count;
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
        return displayed_ref_count > 0;
    }

    /**
     * @returns @c true if tabbox is shown, @c false if replaced by Effect
     */
    bool is_shown() const
    {
        return is_natively_shown;
    }

    bool handle_mouse_event(QMouseEvent* event)
    {
        if (!is_natively_shown && is_displayed()) {
            // tabbox has been replaced, check effects
            if (auto& effects = space.base.render->compositor->effects;
                effects && effects->checkInputWindowEvent(event)) {
                return true;
            }
        }
        switch (event->type()) {
        case QEvent::MouseMove:
            if (!handler->contains_pos(event->globalPos())) {
                // filter out all events which are not on the TabBox window.
                // We don't want windows to react on the mouse events
                return true;
            }
            return false;
        case QEvent::MouseButtonPress:
            if ((!is_natively_shown && is_displayed())
                || !handler->contains_pos(event->globalPos())) {
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
        if (!is_natively_shown && is_displayed()) {
            // tabbox has been replaced, check effects
            if (auto& effects = space.base.render->compositor->effects;
                effects && effects->checkInputWindowEvent(event)) {
                return true;
            }
        }
        if (event->angleDelta().y() == 0) {
            return false;
        }
        const QModelIndex index = handler->next_prev(event->angleDelta().y() > 0);
        if (index.isValid()) {
            set_current_index(index);
        }
        return true;
    }

    void grabbed_key_event(QKeyEvent* event)
    {
        Q_EMIT qobject->tabbox_key_event(event);
        if (!is_natively_shown && is_displayed()) {
            // tabbox has been replaced, check effects
            return;
        }
        if (grab.no_modifier) {
            if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return
                || event->key() == Qt::Key_Space) {
                accept();
                return;
            }
        }
        handler->grabbed_key_event(event);
    }

    bool is_grabbed() const
    {
        return grab.tab || grab.desktop;
    }

    void init_shortcuts()
    {
        key(
            s_windows, [this] { slot_walk_through_windows(); }, Qt::ALT | Qt::Key_Tab);
        key(
            s_windowsRev,
            [this] { slot_walk_back_through_windows(); },
            Qt::ALT | Qt::SHIFT | Qt::Key_Backtab);
        key(
            s_app,
            [this] { slot_walk_through_current_app_windows(); },
            Qt::ALT | Qt::Key_QuoteLeft);
        key(
            s_appRev,
            [this] { slot_walk_back_through_current_app_windows(); },
            Qt::ALT | Qt::Key_AsciiTilde);
        key(s_windowsAlt, [this] { slot_walk_through_windows_alternative(); });
        key(s_windowsAltRev, [this] { slot_walk_back_through_windows_alternative(); });
        key(s_appAlt, [this] { slot_walk_through_current_app_windows_alternative(); });
        key(s_appAltRev, [this] { slot_walk_back_through_current_app_windows_alternative(); });
        key(s_desktops, [this] { slot_walk_through_desktops(); });
        key(s_desktopsRev, [this] { slot_walk_back_through_desktops(); });
        key(s_desktopList, [this] { slot_walk_through_desktop_list(); });
        key(s_desktopListRev, [this] { slot_walk_back_through_desktop_list(); });

        QObject::connect(
            space.base.input->shortcuts.get(),
            &decltype(space.base.input->shortcuts)::element_type::keyboard_shortcut_changed,
            qobject.get(),
            [this](auto action, auto const& seq) { global_shortcut_changed(action, seq); });
    }

    /// Travers all clients according to static order. Useful for CDE-style Alt-tab feature.
    std::optional<window_t> next_client_static(std::optional<window_t> c) const
    {
        auto const& list = get_windows_with_control(space.windows);
        if (!c || list.empty()) {
            return {};
        }
        auto pos = index_of(list, *c);
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
    std::optional<window_t> previous_client_static(std::optional<window_t> c) const
    {
        auto const& list = get_windows_with_control(space.windows);
        if (!c || list.empty()) {
            return {};
        }

        auto pos = index_of(list, *c);
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

        auto contains = [](QKeySequence const& shortcut, int key) -> bool {
            for (int i = 0; i < shortcut.count(); ++i) {
                if (shortcut[i] == key) {
                    return true;
                }
            }
            return false;
        };

        // tests whether a shortcut matches and handles pitfalls on ShiftKey invocation
        auto direction_for = [keyQt, contains](QKeySequence const& forward,
                                               QKeySequence const& backward) -> Direction {
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

        if (grab.tab) {
            static int const mode_count = 4;
            static tabbox_mode const modes[mode_count]
                = {tabbox_mode::windows,
                   tabbox_mode::windows_alternative,
                   tabbox_mode::current_app_windows,
                   tabbox_mode::current_app_windows_alternative};
            QKeySequence const cuts[2 * mode_count]
                = {// forward
                   walk_sc.windows.normal,
                   walk_sc.windows.alternative,
                   walk_sc.current_app_windows.normal,
                   walk_sc.current_app_windows.alternative,
                   // backward
                   walk_sc.windows.reverse,
                   walk_sc.windows.alternative_reverse,
                   walk_sc.current_app_windows.reverse,
                   walk_sc.current_app_windows.alternative_reverse};

            // in case of collision, prefer to stay in the current mode
            bool tested_current = false;
            int i = 0;
            int j = 0;

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
                } else if (++j > 2 * mode_count) {
                    // guarding counter for invalid modes
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

        } else if (grab.desktop) {
            direction = direction_for(walk_sc.desktops.normal, walk_sc.desktops.reverse);
            if (direction == Steady) {
                direction = direction_for(walk_sc.desktops.list, walk_sc.desktops.list_reverse);
            }
            if (direction != Steady) {
                walk_through_desktops(direction == Forward);
            }
        }

        if (grab.desktop || grab.tab) {
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
        if (grab.no_modifier) {
            return;
        }

        if (grab.tab) {
            bool old_control_grab = grab.desktop;
            accept();
            grab.desktop = old_control_grab;
        }

        if (grab.desktop) {
            bool old_tab_grab = grab.tab;
            int desktop = current_desktop();
            close();
            grab.tab = old_tab_grab;
            if (desktop != -1) {
                set_current_desktop(desktop);
                space.virtual_desktop_manager->setCurrent(desktop);
            }
        }
    }

    bool forced_global_mouse_grab() const
    {
        return grab.forced_global_mouse;
    }

    bool no_modifier_grab() const
    {
        return grab.no_modifier;
    }

    void set_current_index(QModelIndex index, bool notify_effects = true)
    {
        if (!index.isValid()) {
            return;
        }

        handler->set_current_index(index);

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
        Q_EMIT qobject->tabbox_added(current_mode);

        if (is_displayed()) {
            is_natively_shown = false;
            return;
        }

        set_showing_desktop(space, false);
        reference();
        is_natively_shown = true;
        handler->show();
    }

    void close(bool abort = false)
    {
        if (is_grabbed()) {
            remove_tabbox_grab();
        }

        hide(abort);
        space.input->pointer->setEnableConstraints(true);

        grab.tab = false;
        grab.desktop = false;
        grab.no_modifier = false;
    }

    void accept(bool close_tabbox = true)
    {
        auto c = current_client();

        if (close_tabbox) {
            close();
        }

        if (c) {
            std::visit(overload{[&](auto&& win) {
                           activate_window(space, *win);
                           if (win::is_desktop(win)) {
                               set_showing_desktop(space, !space.showing_desktop);
                           }
                       }},
                       *c);
        }
    }

    void slot_walk_through_desktops()
    {
        if (!config_is_ready || is_grabbed()) {
            return;
        }

        if (areModKeysDepressed(*space.base.input, walk_sc.desktops.normal)) {
            if (start_walk_through_desktops()) {
                walk_through_desktops(true);
            }
        } else {
            one_step_through_desktops(true);
        }
    }

    void slot_walk_back_through_desktops()
    {
        if (!config_is_ready || is_grabbed()) {
            return;
        }

        if (areModKeysDepressed(*space.base.input, walk_sc.desktops.reverse)) {
            if (start_walk_through_desktops()) {
                walk_through_desktops(false);
            }
        } else {
            one_step_through_desktops(false);
        }
    }

    void slot_walk_through_desktop_list()
    {
        if (!config_is_ready || is_grabbed()) {
            return;
        }

        if (areModKeysDepressed(*space.base.input, walk_sc.desktops.list)) {
            if (start_walk_through_desktop_list()) {
                walk_through_desktops(true);
            }
        } else {
            one_step_through_desktop_list(true);
        }
    }

    void slot_walk_back_through_desktop_list()
    {
        if (!config_is_ready || is_grabbed()) {
            return;
        }

        if (areModKeysDepressed(*space.base.input, walk_sc.desktops.list_reverse)) {
            if (start_walk_through_desktop_list()) {
                walk_through_desktops(false);
            }
        } else {
            one_step_through_desktop_list(false);
        }
    }

    void slot_walk_through_windows()
    {
        navigating_through_windows(true, walk_sc.windows.normal, tabbox_mode::windows);
    }

    void slot_walk_back_through_windows()
    {
        navigating_through_windows(false, walk_sc.windows.reverse, tabbox_mode::windows);
    }

    void slot_walk_through_windows_alternative()
    {
        navigating_through_windows(
            true, walk_sc.windows.alternative, tabbox_mode::windows_alternative);
    }

    void slot_walk_back_through_windows_alternative()
    {
        navigating_through_windows(
            false, walk_sc.windows.alternative_reverse, tabbox_mode::windows_alternative);
    }

    void slot_walk_through_current_app_windows()
    {
        navigating_through_windows(
            true, walk_sc.current_app_windows.normal, tabbox_mode::current_app_windows);
    }

    void slot_walk_back_through_current_app_windows()
    {
        navigating_through_windows(
            false, walk_sc.current_app_windows.reverse, tabbox_mode::current_app_windows);
    }

    void slot_walk_through_current_app_windows_alternative()
    {
        navigating_through_windows(true,
                                   walk_sc.current_app_windows.alternative,
                                   tabbox_mode::current_app_windows_alternative);
    }

    void slot_walk_back_through_current_app_windows_alternative()
    {
        navigating_through_windows(false,
                                   walk_sc.current_app_windows.alternative_reverse,
                                   tabbox_mode::current_app_windows_alternative);
    }

    void set_handler_ready()
    {
        handler->set_config(config.normal);
        reconfigure();
        config_is_ready = true;
    }

    bool toggle(electric_border eb)
    {
        if (border_activate.alternative.find(eb) != border_activate.alternative.end()) {
            return toggle_mode(tabbox_mode::windows_alternative);
        }

        return toggle_mode(tabbox_mode::windows);
    }

    std::unique_ptr<tabbox_qobject> qobject;
    Space& space;

private:
    template<typename Input>
    bool areModKeysDepressed(Input const& input, QKeySequence const& seq)
    {
        if (seq.isEmpty()) {
            return false;
        }

        return input.are_mod_keys_depressed(seq);
    }

    static std::vector<window_t> get_windows_with_control(std::vector<window_t>& windows)
    {
        std::vector<window_t> with_control;

        for (auto win : windows) {
            std::visit(overload{[&](auto&& win) {
                           if (win->control) {
                               with_control.push_back(win);
                           }
                       }},
                       win);
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

    // tabbox_mode::windows | tabbox_mode::windows_alternative
    bool start_kde_walk_through_windows(tabbox_mode mode)
    {
        if (!establish_tabbox_grab()) {
            return false;
        }
        grab.tab = true;
        grab.no_modifier = false;
        set_mode(mode);
        reset();
        return true;
    }

    // tabbox_mode::desktop | tabbox_mode::desktop_list
    bool start_walk_through_desktops(tabbox_mode mode)
    {
        if (!establish_tabbox_grab()) {
            return false;
        }

        grab.desktop = true;
        grab.no_modifier = false;

        set_mode(mode);
        reset();

        // Show the switcher only when there are two or more clients.
        if (handler->client_list().size() <= 1) {
            close();
            return false;
        }

        return true;
    }

    bool start_walk_through_desktops()
    {
        return start_walk_through_desktops(tabbox_mode::desktop);
    }

    bool start_walk_through_desktop_list()
    {
        return start_walk_through_desktops(tabbox_mode::desktop_list);
    }

    // TabBoxWindowsMode | TabBoxWindowsAlternativeMode
    void navigating_through_windows(bool forward, QKeySequence const& shortcut, tabbox_mode mode)
    {
        if (!config_is_ready || is_grabbed()) {
            return;
        }

        if (!space.options->qobject->focusPolicyIsReasonable()) {
            // ungrabXKeyboard(); // need that because of accelerator raw mode
            //  CDE style raise / lower
            cde_walk_through_windows(forward);
        } else {
            if (areModKeysDepressed(*space.base.input, shortcut)) {
                if (start_kde_walk_through_windows(mode)) {
                    kde_walk_through_windows(forward);
                }
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
        std::optional<window_t> old_top_win;

        // This function find the first suitable client for unreasonable focus policies - the
        // topmost one with exceptions (can't be keepabove/below, otherwise gets stuck on them).
        for (int i = space.stacking.order.stack.size() - 1; i >= 0; --i) {
            if (std::visit(overload{[&](auto&& win) {
                               if (win->control && on_current_desktop(win)
                                   && !win::is_special_window(win) && win->isShown()
                                   && win::wants_tab_focus(win) && !win->control->keep_above
                                   && !win->control->keep_below) {
                                   old_top_win = win;
                                   return true;
                               }
                               return false;
                           }},
                           space.stacking.order.stack.at(i))) {
                break;
            }
        }

        auto candidate = old_top_win;
        std::optional<window_t> first_win;

        bool options_traverse_all;
        {
            KConfigGroup group(space.base.config.main, "TabBox");
            options_traverse_all = group.readEntry("TraverseAll", false);
        }

        auto accept = [&, this](auto&& win) {
            if (!win || win == old_top_win) {
                // No candidate anymore, looped around. Abort looping.
                return true;
            }

            return std::visit(overload{[&](auto&& win) {
                                  if (win->control->minimized || !win::wants_tab_focus(win)
                                      || win->control->keep_above || win->control->keep_below) {
                                      return false;
                                  }
                                  if (!options_traverse_all
                                      && !on_desktop(win, current_desktop())) {
                                      return false;
                                  }
                                  return true;
                              }},
                              *win);
        };

        do {
            candidate = forward ? next_client_static(candidate) : previous_client_static(candidate);
            if (!first_win) {
                // When we see our first client for the second time, it's time to stop.
                first_win = candidate;
                continue;
            }

            if (candidate == first_win) {
                // No candidates found.
                candidate = {};
            }
        } while (!accept(candidate));

        if (!candidate) {
            return;
        }

        if (old_top_win && old_top_win != candidate) {
            std::visit(overload{[&](auto&& win) { win::lower_window(space, win); }}, *old_top_win);
        }

        std::visit(overload{[&](auto&& win) {
                       if (space.options->qobject->focusPolicyIsReasonable()) {
                           activate_window(space, *win);
                           return;
                       }

                       if (!on_desktop(win, current_desktop())) {
                           set_current_desktop(get_desktop(*win));
                       }
                       win::raise_window(space, win);
                   }},
                   *candidate);
    }

    void walk_through_desktops(bool forward)
    {
        next_prev(forward);
        delayed_show();
    }

    // TabBoxWindowsMode | TabBoxWindowsAlternativeMode
    void kde_one_step_through_windows(bool forward, tabbox_mode mode)
    {
        set_mode(mode);
        reset();
        next_prev(forward);
        if (auto win = current_client()) {
            std::visit(overload{[&](auto&& win) { activate_window(space, *win); }}, *win);
        }
    }

    // TabBoxDesktopMode | TabBoxDesktopListMode
    void one_step_through_desktops(bool forward, tabbox_mode mode)
    {
        set_mode(mode);
        reset();
        next_prev(forward);
        if (current_desktop() != -1) {
            set_current_desktop(current_desktop());
        }
    }

    void one_step_through_desktops(bool forward)
    {
        one_step_through_desktops(forward, tabbox_mode::desktop);
    }

    void one_step_through_desktop_list(bool forward)
    {
        one_step_through_desktops(forward, tabbox_mode::desktop_list);
    }

    bool establish_tabbox_grab()
    {
        if constexpr (requires(decltype(space.base.input) input) { input->ungrab_keyboard(); }) {
            return establish_tabbox_grab_x11();
        } else {
            grab.forced_global_mouse = true;
            return true;
        }
    }

    bool establish_tabbox_grab_x11()
    {
        base::x11::update_time_from_clock(space.base);

        if (!space.base.input->grab_keyboard()) {
            return false;
        }

        // Don't try to establish a global mouse grab using XGrabPointer, as that would prevent
        // using Alt+Tab while DND (#44972). However force passive grabs on all windows
        // in order to catch MouseRelease events and close the tabbox (#67416).
        // All clients already have passive grabs in their wrapper windows, so check only
        // the active client, which may not have it.
        Q_ASSERT(!grab.forced_global_mouse);
        grab.forced_global_mouse = true;

        if (space.stacking.active) {
            std::visit(overload{[&](auto&& win) { win->control->update_mouse_grab(); }},
                       *space.stacking.active);
        }

        x11_event_filter.reset(new tabbox_x11_filter<tabbox<Space>>(*this));
        return true;
    }

    void remove_tabbox_grab()
    {
        if constexpr (requires(decltype(space.base.input) input) { input->ungrab_keyboard(); }) {
            remove_tabbox_grab_x11();
        } else {
            grab.forced_global_mouse = false;
        }
    }

    void remove_tabbox_grab_x11()
    {
        base::x11::update_time_from_clock(space.base);
        space.base.input->ungrab_keyboard();

        Q_ASSERT(grab.forced_global_mouse);
        grab.forced_global_mouse = false;
        if (space.stacking.active) {
            std::visit(overload{[](auto&& win) { win->control->update_mouse_grab(); }},
                       *space.stacking.active);
        }
        x11_event_filter.reset();
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

        space.base.input->shortcuts->register_keyboard_shortcut(a,
                                                                QList<QKeySequence>() << shortcut);
        space.base.input->registerShortcut(shortcut, a, qobject.get(), slot);

        auto cuts = space.base.input->shortcuts->get_keyboard_shortcut(a);
        global_shortcut_changed(a, cuts.isEmpty() ? QKeySequence() : cuts.first());
    }

    bool toggle_mode(tabbox_mode mode)
    {
        if (!space.options->qobject->focusPolicyIsReasonable()) {
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
        grab.no_modifier = grab.tab = true;
        set_mode(mode);
        reset();
        show();
        return true;
    }

    void reconfigure()
    {
        auto cfg = space.base.config.main;
        auto cfg_group = cfg->group("TabBox");

        load_config(cfg->group("TabBox"), config.normal);
        load_config(cfg->group("TabBoxAlternative"), config.alternative);

        config.normal_current_app = config.normal;
        config.normal_current_app.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);
        config.alternative_current_app = config.alternative;
        config.alternative_current_app.set_client_applications_mode(
            tabbox_config::AllWindowsCurrentApplication);

        handler->set_config(config.normal);
        delay_show_data.duration = cfg_group.template readEntry<int>("DelayTime", 90);

        QString const default_desktop_layout = QStringLiteral("org.kde.breeze.desktop");
        config.desktop.set_layout_name(
            cfg_group.readEntry("DesktopLayout", default_desktop_layout));
        config.desktop_list.set_layout_name(
            cfg_group.readEntry("DesktopListLayout", default_desktop_layout));

        auto recreate_borders = [this, &cfg_group](auto& borders, auto const& border_config) {
            for (auto const& [border, id] : borders) {
                space.edges->unreserve(border, id);
            }

            borders.clear();
            auto list = cfg_group.readEntry(border_config, QStringList());

            for (auto const& s : qAsConst(list)) {
                bool ok;
                auto i = s.toInt(&ok);
                if (!ok) {
                    continue;
                }
                auto border = static_cast<electric_border>(i);
                auto id = space.edges->reserve(border, [this](auto eb) { return toggle(eb); });
                borders.insert({border, id});
            }
        };

        recreate_borders(border_activate.normal, QStringLiteral("BorderActivate"));
        recreate_borders(border_activate.alternative, QStringLiteral("BorderAlternativeActivate"));

        auto touch_cfg = [this, cfg_group](QString const& key,
                                           QHash<electric_border, QAction*>& actions,
                                           tabbox_mode mode,
                                           QStringList const& defaults = QStringList{}) {
            // fist erase old config
            for (auto it = actions.begin(); it != actions.end();) {
                delete it.value();
                it = actions.erase(it);
            }

            // now new config
            auto const list = cfg_group.readEntry(key, defaults);
            for (auto const& s : list) {
                bool ok;
                auto const i = s.toInt(&ok);
                if (!ok) {
                    continue;
                }
                auto a = new QAction(qobject.get());
                QObject::connect(
                    a, &QAction::triggered, qobject.get(), [this, mode] { toggle_mode(mode); });
                space.edges->reserveTouch(static_cast<electric_border>(i), a);
                actions.insert(static_cast<electric_border>(i), a);
            }
        };

        touch_cfg(QStringLiteral("TouchBorderActivate"),
                  touch_border_action.activate,
                  tabbox_mode::windows);
        touch_cfg(QStringLiteral("TouchBorderAlternativeActivate"),
                  touch_border_action.alternative_activate,
                  tabbox_mode::windows_alternative);
    }

    void global_shortcut_changed(QAction* action, const QKeySequence& seq)
    {
        if (qstrcmp(qPrintable(action->objectName()), s_windows.untranslatedText()) == 0) {
            walk_sc.windows.normal = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_windowsRev.untranslatedText())
                   == 0) {
            walk_sc.windows.reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_app.untranslatedText()) == 0) {
            walk_sc.current_app_windows.normal = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_appRev.untranslatedText()) == 0) {
            walk_sc.current_app_windows.reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAlt.untranslatedText())
                   == 0) {
            walk_sc.windows.alternative = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAltRev.untranslatedText())
                   == 0) {
            walk_sc.windows.alternative_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_appAlt.untranslatedText()) == 0) {
            walk_sc.current_app_windows.alternative = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_appAltRev.untranslatedText()) == 0) {
            walk_sc.current_app_windows.alternative_reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktops.untranslatedText()) == 0) {
            walk_sc.desktops.normal = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktopsRev.untranslatedText())
                   == 0) {
            walk_sc.desktops.reverse = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktopList.untranslatedText())
                   == 0) {
            walk_sc.desktops.list = seq;
        } else if (qstrcmp(qPrintable(action->objectName()), s_desktopListRev.untranslatedText())
                   == 0) {
            walk_sc.desktops.list_reverse = seq;
        }
    }

    tabbox_mode current_mode;
    tabbox_handler_impl<tabbox<Space>>* handler;

    // false if an effect has referenced the tabbox
    // true if tabbox is active (independent on showTabbox setting)
    bool is_natively_shown{false};
    int displayed_ref_count{0};

    // indicates whether the config is completely loaded
    bool config_is_ready{false};

    struct {
        int duration;
        QTimer timer;
    } delay_show_data;

    struct {
        tabbox_config normal;
        tabbox_config alternative;
        tabbox_config normal_current_app;
        tabbox_config alternative_current_app;
        tabbox_config desktop;
        tabbox_config desktop_list;
    } config;

    struct {
        // Shortcuts to walk through items.
        struct {
            QKeySequence normal;
            QKeySequence reverse;
            QKeySequence list;
            QKeySequence list_reverse;
        } desktops;

        struct {
            QKeySequence normal;
            QKeySequence reverse;
            QKeySequence alternative;
            QKeySequence alternative_reverse;
        } windows;

        struct {
            QKeySequence normal;
            QKeySequence reverse;
            QKeySequence alternative;
            QKeySequence alternative_reverse;
        } current_app_windows;
    } walk_sc;

    struct {
        bool desktop{false};
        bool tab{false};

        // true if tabbox is in modal mode which does not require holding a modifier
        bool no_modifier{false};
        bool forced_global_mouse{false};
    } grab;

    struct {
        std::unordered_map<electric_border, uint32_t> normal;
        std::unordered_map<electric_border, uint32_t> alternative;
    } border_activate;

    struct {
        QHash<electric_border, QAction*> activate;
        QHash<electric_border, QAction*> alternative_activate;
    } touch_border_action;

    QScopedPointer<base::x11::event_filter> x11_event_filter;

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
