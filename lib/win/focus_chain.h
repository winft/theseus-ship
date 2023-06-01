/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include "types.h"

#include <list>
#include <optional>
#include <unordered_map>

namespace KWin::win
{

/**
 * @brief Data struct to handle the various focus chains.
 *
 * A focus chain is a list of Clients containing information on which Client should be activated.
 *
 * This focus_chain holds multiple independent chains. There is one chain of most recently used
 * Clients which is primarily used by TabBox to build up the list of Clients for navigation. The
 * chains are organized as a normal QList of Clients with the most recently used Client being the
 * last item of the list, that is a LIFO like structure.
 *
 * In addition there is one chain for each virtual desktop which is used to determine which Client
 * should get activated when the user switches to another virtual desktop.
 */
template<typename Window>
class focus_chain
{
public:
    using focus_chain_list = std::list<Window>;

    struct {
        focus_chain_list latest_use;
        std::unordered_map<unsigned int, focus_chain_list> desktops;
    } chains;

    std::optional<Window> active_window;
    unsigned int current_desktop{0};

    bool has_separate_screen_focus{false};
};

}
