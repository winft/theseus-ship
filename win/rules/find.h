/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "base/logging.h"

namespace KWin::win::rules
{

template<typename Book, typename RefWin>
window find_window(Book& book, RefWin& ref_win, bool ignore_temporary)
{
    QVector<ruling*> ret;

    for (auto it = book.m_rules.begin(); it != book.m_rules.end();) {
        if (ignore_temporary && (*it)->isTemporary()) {
            ++it;
            continue;
        }

        if ((*it)->match(&ref_win)) {
            auto rule = *it;
            qCDebug(KWIN_CORE) << "Rule found:" << rule << ":" << &ref_win;
            if (rule->isTemporary()) {
                it = book.m_rules.erase(it);
            } else {
                ++it;
            }
            ret.append(rule);
            continue;
        }
        ++it;
    }

    return rules::window(ret);
}

}
