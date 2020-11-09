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
#include "focuschain.h"
#include "abstract_client.h"
#include "screens.h"
#include "win/win.h"

namespace KWin
{

KWIN_SINGLETON_FACTORY_VARIABLE(FocusChain, s_manager)

FocusChain::FocusChain(QObject *parent)
    : QObject(parent)
    , m_separateScreenFocus(false)
    , m_activeClient(nullptr)
    , m_currentDesktop(0)
{
}

FocusChain::~FocusChain()
{
    s_manager = nullptr;
}

void FocusChain::remove(Toplevel* window)
{
    for (auto it = m_desktopFocusChains.begin();
            it != m_desktopFocusChains.end();
            ++it) {
        it.value().removeAll(window);
    }
    m_mostRecentlyUsed.removeAll(window);
}

void FocusChain::resize(uint previousSize, uint newSize)
{
    for (uint i = previousSize + 1; i <= newSize; ++i) {
        m_desktopFocusChains.insert(i, Chain());
    }
    for (uint i = previousSize; i > newSize; --i) {
        m_desktopFocusChains.remove(i);
    }
}

Toplevel* FocusChain::getForActivation(uint desktop) const
{
    return getForActivation(desktop, screens()->current());
}

Toplevel* FocusChain::getForActivation(uint desktop, int screen) const
{
    auto it = m_desktopFocusChains.constFind(desktop);
    if (it == m_desktopFocusChains.constEnd()) {
        return nullptr;
    }
    const auto &chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto tmp = chain.at(i);
        // TODO: move the check into Client
        if (tmp->isShown(false) && tmp->isOnCurrentActivity()
            && ( !m_separateScreenFocus || tmp->screen() == screen)) {
            return tmp;
        }
    }
    return nullptr;
}

void FocusChain::update(Toplevel* window, FocusChain::Change change)
{
    if (!win::wants_tab_focus(window)) {
        // Doesn't want tab focus, remove
        remove(window);
        return;
    }

    if (window->isOnAllDesktops()) {
        // Now on all desktops, add it to focus chains it is not already in
        for (auto it = m_desktopFocusChains.begin();
                it != m_desktopFocusChains.end();
                ++it) {
            auto &chain = it.value();
            // Making first/last works only on current desktop, don't affect all desktops
            if (it.key() == m_currentDesktop
                    && (change == MakeFirst || change == MakeLast)) {
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
        for (auto it = m_desktopFocusChains.begin();
                it != m_desktopFocusChains.end();
                ++it) {
            auto &chain = it.value();
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

void FocusChain::updateClientInChain(Toplevel* window, FocusChain::Change change, Chain &chain)
{
    if (change == MakeFirst) {
        makeFirstInChain(window, chain);
    } else if (change == MakeLast) {
        makeLastInChain(window, chain);
    } else {
        insertClientIntoChain(window, chain);
    }
}

void FocusChain::insertClientIntoChain(Toplevel* window, Chain &chain)
{
    if (chain.contains(window)) {
        return;
    }
    if (m_activeClient && m_activeClient != window &&
            !chain.empty() && chain.last() == m_activeClient) {
        // Add it after the active client
        chain.insert(chain.size() - 1, window);
    } else {
        // Otherwise add as the first one
        chain.append(window);
    }
}

void FocusChain::moveAfterClient(Toplevel *window, Toplevel* reference)
{
    if (!win::wants_tab_focus(window)) {
        return;
    }

    for (auto it = m_desktopFocusChains.begin();
            it != m_desktopFocusChains.end();
            ++it) {
        if (!window->isOnDesktop(it.key())) {
            continue;
        }
        moveAfterClientInChain(window, reference, it.value());
    }
    moveAfterClientInChain(window, reference, m_mostRecentlyUsed);
}

void FocusChain::moveAfterClientInChain(Toplevel* window, Toplevel* reference, Chain &chain)
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

Toplevel* FocusChain::firstMostRecentlyUsed() const
{
    if (m_mostRecentlyUsed.isEmpty()) {
        return nullptr;
    }
    return m_mostRecentlyUsed.first();
}

Toplevel* FocusChain::nextMostRecentlyUsed(Toplevel* reference) const
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
bool FocusChain::isUsableFocusCandidate(Toplevel* window, Toplevel* prev) const
{
    return window != prev &&
           window->isShown(false) && window->isOnCurrentDesktop() && window->isOnCurrentActivity() &&
           (!m_separateScreenFocus || win::on_screen(window, prev ? prev->screen() : screens()->current()));
}

Toplevel* FocusChain::nextForDesktop(Toplevel* reference, uint desktop) const
{
    auto it = m_desktopFocusChains.constFind(desktop);
    if (it == m_desktopFocusChains.constEnd()) {
        return nullptr;
    }
    const auto &chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto client = chain.at(i);
        if (isUsableFocusCandidate(client, reference)) {
            return client;
        }
    }
    return nullptr;
}

void FocusChain::makeFirstInChain(Toplevel *window, Chain &chain)
{
    chain.removeAll(window);
    chain.append(window);
}

void FocusChain::makeLastInChain(Toplevel *window, Chain &chain)
{
    chain.removeAll(window);
    chain.prepend(window);
}

bool FocusChain::contains(Toplevel *window, uint desktop) const
{
    auto it = m_desktopFocusChains.constFind(desktop);
    if (it == m_desktopFocusChains.constEnd()) {
        return false;
    }
    return it.value().contains(window);
}

} // namespace
