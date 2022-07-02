/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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
#include "focus_chain.h"

#include "controlling.h"
#include "screen.h"
#include "util.h"

#include "toplevel.h"

namespace KWin::win
{

focus_chain::focus_chain(win::space& space)
    : space{space}
{
}

void focus_chain::remove(Toplevel* window)
{
    for (auto it = chains.desktops.begin(); it != chains.desktops.end(); ++it) {
        it.value().removeAll(window);
    }
    chains.latest_use.removeAll(window);
}

void focus_chain::resize(uint previousSize, uint newSize)
{
    for (uint i = previousSize + 1; i <= newSize; ++i) {
        chains.desktops.insert(i, Chain());
    }
    for (uint i = previousSize; i > newSize; --i) {
        chains.desktops.remove(i);
    }
}

Toplevel* focus_chain::getForActivation(uint desktop) const
{
    return getForActivation(desktop, get_current_output(space));
}

Toplevel* focus_chain::getForActivation(uint desktop, base::output const* output) const
{
    auto it = chains.desktops.constFind(desktop);
    if (it == chains.desktops.constEnd()) {
        return nullptr;
    }
    const auto& chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto tmp = chain.at(i);
        // TODO: move the check into Client
        if (tmp->isShown() && (!has_separate_screen_focus || tmp->central_output == output)) {
            return tmp;
        }
    }
    return nullptr;
}

void focus_chain::update(Toplevel* window, focus_chain_change change)
{
    if (!win::wants_tab_focus(window)) {
        // Doesn't want tab focus, remove
        remove(window);
        return;
    }

    if (window->isOnAllDesktops()) {
        // Now on all desktops, add it to focus chains it is not already in
        for (auto it = chains.desktops.begin(); it != chains.desktops.end(); ++it) {
            auto& chain = it.value();
            // Making first/last works only on current desktop, don't affect all desktops
            if (it.key() == current_desktop
                && (change == focus_chain_change::make_first
                    || change == focus_chain_change::make_last)) {
                if (change == focus_chain_change::make_first) {
                    makeFirstInChain(window, chain);
                } else {
                    makeLastInChain(window, chain);
                }
            } else {
                insertClientIntoChain(window, chain);
            }
        }
    } else {
        // Now only on desktop, remove it anywhere else
        for (auto it = chains.desktops.begin(); it != chains.desktops.end(); ++it) {
            auto& chain = it.value();
            if (window->isOnDesktop(it.key())) {
                updateClientInChain(window, change, chain);
            } else {
                chain.removeAll(window);
            }
        }
    }

    // add for most recently used chain
    updateClientInChain(window, change, chains.latest_use);
}

void focus_chain::updateClientInChain(Toplevel* window, focus_chain_change change, Chain& chain)
{
    if (change == focus_chain_change::make_first) {
        makeFirstInChain(window, chain);
    } else if (change == focus_chain_change::make_last) {
        makeLastInChain(window, chain);
    } else {
        insertClientIntoChain(window, chain);
    }
}

void focus_chain::insertClientIntoChain(Toplevel* window, Chain& chain)
{
    if (chain.contains(window)) {
        return;
    }
    if (active_window && active_window != window && !chain.empty()
        && chain.last() == active_window) {
        // Add it after the active client
        chain.insert(chain.size() - 1, window);
    } else {
        // Otherwise add as the first one
        chain.append(window);
    }
}

void focus_chain::moveAfterClient(Toplevel* window, Toplevel* reference)
{
    if (!win::wants_tab_focus(window)) {
        return;
    }

    for (auto it = chains.desktops.begin(); it != chains.desktops.end(); ++it) {
        if (!window->isOnDesktop(it.key())) {
            continue;
        }
        moveAfterClientInChain(window, reference, it.value());
    }
    moveAfterClientInChain(window, reference, chains.latest_use);
}

void focus_chain::moveAfterClientInChain(Toplevel* window, Toplevel* reference, Chain& chain)
{
    if (!chain.contains(reference)) {
        return;
    }
    if (win::belong_to_same_client(reference, window)) {
        chain.removeAll(window);
        chain.insert(chain.indexOf(reference), window);
    } else {
        chain.removeAll(window);
        for (int i = chain.size() - 1; i >= 0; --i) {
            if (win::belong_to_same_client(reference, chain.at(i))) {
                chain.insert(i, window);
                break;
            }
        }
    }
}

Toplevel* focus_chain::firstMostRecentlyUsed() const
{
    if (chains.latest_use.isEmpty()) {
        return nullptr;
    }
    return chains.latest_use.first();
}

Toplevel* focus_chain::nextMostRecentlyUsed(Toplevel* reference) const
{
    if (chains.latest_use.isEmpty()) {
        return nullptr;
    }
    const int index = chains.latest_use.indexOf(reference);
    if (index == -1) {
        return chains.latest_use.first();
    }
    if (index == 0) {
        return chains.latest_use.last();
    }
    return chains.latest_use.at(index - 1);
}

// copied from activation.cpp
bool focus_chain::isUsableFocusCandidate(Toplevel* window, Toplevel* prev) const
{
    return window != prev && window->isShown() && window->isOnCurrentDesktop()
        && (!has_separate_screen_focus
            || win::on_screen(window, prev ? prev->central_output : get_current_output(space)));
}

Toplevel* focus_chain::nextForDesktop(Toplevel* reference, uint desktop) const
{
    auto it = chains.desktops.constFind(desktop);
    if (it == chains.desktops.constEnd()) {
        return nullptr;
    }
    const auto& chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto client = chain.at(i);
        if (isUsableFocusCandidate(client, reference)) {
            return client;
        }
    }
    return nullptr;
}

void focus_chain::makeFirstInChain(Toplevel* window, Chain& chain)
{
    chain.removeAll(window);
    chain.append(window);
}

void focus_chain::makeLastInChain(Toplevel* window, Chain& chain)
{
    chain.removeAll(window);
    chain.prepend(window);
}

bool focus_chain::contains(Toplevel* window, uint desktop) const
{
    auto it = chains.desktops.constFind(desktop);
    if (it == chains.desktops.constEnd()) {
        return false;
    }
    return it.value().contains(window);
}

}
