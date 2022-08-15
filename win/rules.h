/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "rules/book.h"

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

}
