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

#include "toplevel.h"

#include "win/controlling.h"
#include "win/screen.h"
#include "win/util.h"

namespace KWin::win
{

KWIN_SINGLETON_FACTORY_VARIABLE(focus_chain, s_manager)

focus_chain::focus_chain(QObject* parent)
    : QObject(parent)
    , m_separateScreenFocus(false)
    , m_activeClient(nullptr)
    , m_currentDesktop(0)
{
}

focus_chain::~focus_chain()
{
    s_manager = nullptr;
}

void focus_chain::remove(Toplevel* window)
{
    for (auto it = desktop_focus_chains.begin(); it != desktop_focus_chains.end(); ++it) {
        it.value().removeAll(window);
    }
    m_mostRecentlyUsed.removeAll(window);
}

void focus_chain::resize(uint previousSize, uint newSize)
{
    for (uint i = previousSize + 1; i <= newSize; ++i) {
        desktop_focus_chains.insert(i, Chain());
    }
    for (uint i = previousSize; i > newSize; --i) {
        desktop_focus_chains.remove(i);
    }
}

Toplevel* focus_chain::getForActivation(uint desktop) const
{
    return getForActivation(desktop, get_current_output(*workspace()));
}

Toplevel* focus_chain::getForActivation(uint desktop, int screen) const
{
    auto it = desktop_focus_chains.constFind(desktop);
    if (it == desktop_focus_chains.constEnd()) {
        return nullptr;
    }
    const auto& chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto tmp = chain.at(i);
        // TODO: move the check into Client
        if (tmp->isShown()
            && (!m_separateScreenFocus
                || tmp->central_output
                    == base::get_output(kwinApp()->get_base().get_outputs(), screen))) {
            return tmp;
        }
    }
    return nullptr;
}

void focus_chain::update(Toplevel* window, focus_chain::Change change)
{
    if (!win::wants_tab_focus(window)) {
        // Doesn't want tab focus, remove
        remove(window);
        return;
    }

    if (window->isOnAllDesktops()) {
        // Now on all desktops, add it to focus chains it is not already in
        for (auto it = desktop_focus_chains.begin(); it != desktop_focus_chains.end(); ++it) {
            auto& chain = it.value();
            // Making first/last works only on current desktop, don't affect all desktops
            if (it.key() == m_currentDesktop && (change == MakeFirst || change == MakeLast)) {
                if (change == MakeFirst) {
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
        for (auto it = desktop_focus_chains.begin(); it != desktop_focus_chains.end(); ++it) {
            auto& chain = it.value();
            if (window->isOnDesktop(it.key())) {
                updateClientInChain(window, change, chain);
            } else {
                chain.removeAll(window);
            }
        }
    }

    // add for most recently used chain
    updateClientInChain(window, change, m_mostRecentlyUsed);
}

void focus_chain::updateClientInChain(Toplevel* window, focus_chain::Change change, Chain& chain)
{
    if (change == MakeFirst) {
        makeFirstInChain(window, chain);
    } else if (change == MakeLast) {
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
    if (m_activeClient && m_activeClient != window && !chain.empty()
        && chain.last() == m_activeClient) {
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

    for (auto it = desktop_focus_chains.begin(); it != desktop_focus_chains.end(); ++it) {
        if (!window->isOnDesktop(it.key())) {
            continue;
        }
        moveAfterClientInChain(window, reference, it.value());
    }
    moveAfterClientInChain(window, reference, m_mostRecentlyUsed);
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
    if (m_mostRecentlyUsed.isEmpty()) {
        return nullptr;
    }
    return m_mostRecentlyUsed.first();
}

Toplevel* focus_chain::nextMostRecentlyUsed(Toplevel* reference) const
{
    if (m_mostRecentlyUsed.isEmpty()) {
        return nullptr;
    }
    const int index = m_mostRecentlyUsed.indexOf(reference);
    if (index == -1) {
        return m_mostRecentlyUsed.first();
    }
    if (index == 0) {
        return m_mostRecentlyUsed.last();
    }
    return m_mostRecentlyUsed.at(index - 1);
}

// copied from activation.cpp
bool focus_chain::isUsableFocusCandidate(Toplevel* window, Toplevel* prev) const
{
    return window != prev && window->isShown() && window->isOnCurrentDesktop()
        && (!m_separateScreenFocus
            || win::on_screen(window,
                              prev ? base::get_output_index(kwinApp()->get_base().get_outputs(),
                                                            prev->central_output)
                                   : get_current_output(*workspace())));
}

Toplevel* focus_chain::nextForDesktop(Toplevel* reference, uint desktop) const
{
    auto it = desktop_focus_chains.constFind(desktop);
    if (it == desktop_focus_chains.constEnd()) {
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
    auto it = desktop_focus_chains.constFind(desktop);
    if (it == desktop_focus_chains.constEnd()) {
        return false;
    }
    return it.value().contains(window);
}

}
