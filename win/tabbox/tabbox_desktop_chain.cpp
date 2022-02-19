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
#include "tabbox_desktop_chain.h"

namespace KWin
{
namespace win
{

tabbox_desktop_chain::tabbox_desktop_chain(uint initialSize)
    : m_chain(initialSize)
{
    init();
}

void tabbox_desktop_chain::init()
{
    for (int i = 0; i < m_chain.size(); ++i) {
        m_chain[i] = i + 1;
    }
}

uint tabbox_desktop_chain::next(uint index_desktop) const
{
    const int i = m_chain.indexOf(index_desktop);
    if (i >= 0 && i + 1 < m_chain.size()) {
        return m_chain[i + 1];
    } else if (m_chain.size() > 0) {
        return m_chain[0];
    } else {
        return 1;
    }
}

void tabbox_desktop_chain::resize(uint previous_size, uint new_size)
{
    Q_ASSERT(int(previous_size) == m_chain.size());
    m_chain.resize(new_size);

    if (new_size >= previous_size) {
        // We do not destroy the chain in case new desktops are added
        for (uint i = previous_size; i < new_size; ++i) {
            m_chain[i] = i + 1;
        }
    } else {
        // But when desktops are removed, we may have to modify the chain a bit,
        // otherwise invalid desktops may show up.
        for (int i = 0; i < m_chain.size(); ++i) {
            m_chain[i] = qMin(m_chain[i], new_size);
        }
    }
}

void tabbox_desktop_chain::add(uint desktop)
{
    if (m_chain.isEmpty() || int(desktop) > m_chain.count()) {
        return;
    }
    int index = m_chain.indexOf(desktop);
    if (index == -1) {
        // not found - shift all elements by one position
        index = m_chain.size() - 1;
    }
    for (int i = index; i > 0; --i) {
        m_chain[i] = m_chain[i - 1];
    }
    m_chain[0] = desktop;
}

tabbox_desktop_chain_manager::tabbox_desktop_chain_manager(QObject* parent)
    : QObject(parent)
    , m_max_chain_size(0)
{
    m_current_chain = m_chains.insert(QString(), tabbox_desktop_chain());
}

tabbox_desktop_chain_manager::~tabbox_desktop_chain_manager()
{
}

uint tabbox_desktop_chain_manager::next(uint indexDesktop) const
{
    return m_current_chain.value().next(indexDesktop);
}

void tabbox_desktop_chain_manager::resize(uint previous_size, uint new_size)
{
    m_max_chain_size = new_size;
    for (tabbox_desktop_chains::iterator it = m_chains.begin(); it != m_chains.end(); ++it) {
        it.value().resize(previous_size, new_size);
    }
}

void tabbox_desktop_chain_manager::add_desktop(uint previous_desktop, uint current_desktop)
{
    Q_UNUSED(previous_desktop)
    m_current_chain.value().add(current_desktop);
}

void tabbox_desktop_chain_manager::use_chain(const QString& identifier)
{
    if (m_current_chain.key().isNull()) {
        create_first_chain(identifier);
    } else {
        m_current_chain = m_chains.find(identifier);
        if (m_current_chain == m_chains.end()) {
            m_current_chain = add_new_chain(identifier);
        }
    }
}

void tabbox_desktop_chain_manager::create_first_chain(const QString& identifier)
{
    tabbox_desktop_chain value(m_current_chain.value());
    m_chains.erase(m_current_chain);
    m_current_chain = m_chains.insert(identifier, value);
}

QHash<QString, tabbox_desktop_chain>::Iterator
tabbox_desktop_chain_manager::add_new_chain(const QString& identifier)
{
    return m_chains.insert(identifier, tabbox_desktop_chain(m_max_chain_size));
}

} // namespace win
} // namespace KWin
