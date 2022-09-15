/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "activation.h"
#include "controlling.h"
#include "desktop_set.h"
#include "maximize.h"
#include "rules/book.h"
#include "screen.h"
#include "shortcut_set.h"

#include <QObject>
#include <type_traits>

namespace KWin::win
{

template<typename Space>
void init_rule_book(rules::book& book, Space& space)
{
    QObject::connect(
        book.qobject.get(), &rules::book_qobject::updates_enabled, space.qobject.get(), [&] {
            for (auto window : space.windows) {
                if (window->control) {
                    window->updateWindowRules(rules::type::all);
                }
            }
        });

    if (auto manager = space.session_manager.get()) {
        using manager_t = std::remove_pointer_t<decltype(manager)>;
        QObject::connect(
            manager, &manager_t::stateChanged, book.qobject.get(), [&book](auto old, auto next) {
                // If starting to save a session or ending a save session due to either completion
                // or cancellation, we need to disalbe/enable rule book updates.
                auto was_save = old == SessionState::Saving;
                auto will_save = next == SessionState::Saving;
                if (was_save || will_save) {
                    book.setUpdatesDisabled(will_save && !was_save);
                }
            });
    }

    book.load();
}

template<typename Win>
void finish_rules(Win* win)
{
    win->updateWindowRules(rules::type::all);
    win->control->rules = rules::window();
}

// Applies Force, ForceTemporarily and ApplyNow rules
// Used e.g. after the rules have been modified using the kcm.
template<typename Win>
void apply_window_rules(Win& win)
{
    // apply force rules
    // Placement - does need explicit update, just like some others below
    // Geometry : setGeometry() doesn't check rules
    auto client_rules = win.control->rules;

    auto const orig_geom = win.geo.frame;
    auto const geom = client_rules.checkGeometry(orig_geom);

    if (geom != orig_geom) {
        win.setFrameGeometry(geom);
    }

    // MinSize, MaxSize handled by Geometry
    // IgnoreGeometry
    set_desktops(&win, win.topo.desktops);

    // TODO(romangg): can central_output be null?
    send_to_screen(win.space, &win, *win.topo.central_output);
    // Type
    maximize(&win, win.maximizeMode());

    // Minimize : functions don't check
    set_minimized(&win, client_rules.checkMinimize(win.control->minimized));

    set_original_skip_taskbar(&win, win.control->skip_taskbar());
    set_skip_pager(&win, win.control->skip_pager());
    set_skip_switcher(&win, win.control->skip_switcher());
    set_keep_above(&win, win.control->keep_above);
    set_keep_below(&win, win.control->keep_below);
    win.setFullScreen(win.control->fullscreen, true);
    win.setNoBorder(win.noBorder());
    win.updateColorScheme();

    // FSP
    // AcceptFocus :
    if (most_recently_activated_window(win.space) == &win && !client_rules.checkAcceptFocus(true)) {
        activate_next_window(win.space, &win);
    }

    // Closeable
    // TODO(romangg): This is always false because of the size comparison. Remove or fix?
    if (auto s = win.geo.size(); s != win.geo.size() && s.isValid()) {
        constrained_resize(&win, s);
    }

    // Autogrouping : Only checked on window manage
    // AutogroupInForeground : Only checked on window manage
    // AutogroupById : Only checked on window manage
    // StrictGeometry
    set_shortcut(&win, win.control->rules.checkShortcut(win.control->shortcut.toString()));

    // see also X11Client::setActive()
    if (win.control->active) {
        win.setOpacity(win.control->rules.checkOpacityActive(qRound(win.opacity() * 100.0))
                       / 100.0);
        set_global_shortcuts_disabled(win.space,
                                      win.control->rules.checkDisableGlobalShortcuts(false));
    } else {
        win.setOpacity(win.control->rules.checkOpacityInactive(qRound(win.opacity() * 100.0))
                       / 100.0);
    }

    set_desktop_file_name(
        &win, win.control->rules.checkDesktopFile(win.control->desktop_file_name).toUtf8());
}

}
