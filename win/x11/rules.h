/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/data.h"
#include "win/rules/book.h"

#include <KXMessages>

namespace KWin::win::x11
{

inline void rules_setup_book(rules::book& book, base::x11::data const& data)
{
    if (!data.connection) {
        book.m_temporaryRulesMessages.reset();
        return;
    }

    book.m_temporaryRulesMessages = std::make_unique<KXMessages>(
        data.connection, data.root_window, "_KDE_NET_WM_TEMPORARY_RULES", nullptr);
    QObject::connect(book.m_temporaryRulesMessages.get(),
                     &KXMessages::gotMessage,
                     book.qobject.get(),
                     [&book](auto const& message) { book.temporaryRulesMessage(message); });
}

}
