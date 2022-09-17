/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "base/logging.h"

namespace KWin::win::rules
{

template<typename Win>
void setup_rules(Win* win, bool ignore_temporary)
{
    // TODO(romangg): This disconnects all connections of captionChanged to the window itself.
    //                There is only one so this works fine but it's not robustly specified.
    //                Either reshuffle later or use explicit connection object.
    QObject::disconnect(
        win->qobject.get(), &window_qobject::captionChanged, win->qobject.get(), nullptr);
    win->control->rules = find_window(*win->space.rule_book, *win, ignore_temporary);
    // check only after getting the rules, because there may be a rule forcing window type
    // TODO(romangg): what does this mean?
}

template<typename Win>
void evaluate_rules(Win* win)
{
    setup_rules(win, true);
    win->applyWindowRules();
}

template<typename Ruling, typename RefWin>
bool match_rule(Ruling& ruling, RefWin const& ref_win)
{
    if (!ruling.matchType(ref_win.windowType(true))) {
        return false;
    }
    if (!ruling.matchWMClass(ref_win.resource_class, ref_win.resource_name)) {
        return false;
    }
    if (!ruling.matchRole(ref_win.windowRole().toLower())) {
        return false;
    }

    if (auto cm = ref_win.get_client_machine();
        cm && !ruling.matchClientMachine(cm->hostname(), cm->is_local())) {
        return false;
    }

    if (ruling.title.match != name_match::unimportant) {
        // Track title changes to rematch rules.
        auto mutable_client = const_cast<RefWin*>(&ref_win);
        QObject::connect(
            mutable_client->qobject.get(),
            &RefWin::qobject_t::captionChanged,
            mutable_client->qobject.get(),
            [mutable_client] { evaluate_rules(mutable_client); },
            // QueuedConnection, because title may change before
            // the client is ready (could segfault!)
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    }

    return ruling.matchTitle(ref_win.caption.normal);
}

template<typename Book, typename RefWin>
window find_window(Book& book, RefWin& ref_win, bool ignore_temporary)
{
    std::vector<ruling*> ret;

    for (auto it = book.m_rules.begin(); it != book.m_rules.end();) {
        if (ignore_temporary && (*it)->isTemporary()) {
            ++it;
            continue;
        }

        if (match_rule(**it, ref_win)) {
            auto rule = *it;
            qCDebug(KWIN_CORE) << "Rule found:" << rule << ":" << &ref_win;
            if (rule->isTemporary()) {
                it = book.m_rules.erase(it);
            } else {
                ++it;
            }
            ret.push_back(rule);
            continue;
        }
        ++it;
    }

    return rules::window(ret);
}

}
