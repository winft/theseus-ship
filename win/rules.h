/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "rules/rule_book.h"

#include <QObject>

namespace KWin::win
{

template<typename Session>
void init_rule_book(RuleBook& book, Session* manager)
{
    if (manager) {
        QObject::connect(
            manager, &Session::stateChanged, &book, [&book, manager](auto old, auto next) {
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
    win->updateWindowRules(Rules::All);
    win->control->set_rules(WindowRules());
}

}
