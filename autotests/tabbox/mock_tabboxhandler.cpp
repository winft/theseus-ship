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
#include "mock_tabboxhandler.h"
#include "mock_tabboxclient.h"

namespace KWin
{

MockTabBoxHandler::MockTabBoxHandler(QObject* parent)
    : TabBoxHandler(parent)
{
}

MockTabBoxHandler::~MockTabBoxHandler()
{
}

void MockTabBoxHandler::grabbed_key_event(QKeyEvent* event) const
{
    Q_UNUSED(event)
}

std::weak_ptr<win::TabBoxClient> MockTabBoxHandler::active_client() const
{
    return m_activeClient;
}

void MockTabBoxHandler::set_active_client(const std::weak_ptr<win::TabBoxClient>& client)
{
    m_activeClient = client;
}

std::weak_ptr<win::TabBoxClient> MockTabBoxHandler::client_to_add_to_list(win::TabBoxClient* client,
                                                                          int desktop) const
{
    Q_UNUSED(desktop)
    for (auto const& window : m_windows) {
        if (window.get() == client) {
            return window;
        }
    }
    return std::weak_ptr<win::TabBoxClient>();
}

std::weak_ptr<win::TabBoxClient>
MockTabBoxHandler::next_client_focus_chain(win::TabBoxClient* client) const
{
    auto it = m_windows.cbegin();
    for (; it != m_windows.cend(); ++it) {
        if ((*it).get() == client) {
            ++it;
            if (it == m_windows.cend()) {
                return m_windows.front();
            } else {
                return *it;
            }
        }
    }
    if (!m_windows.empty()) {
        return m_windows.back();
    }
    return std::weak_ptr<win::TabBoxClient>();
}

std::weak_ptr<win::TabBoxClient> MockTabBoxHandler::first_client_focus_chain() const
{
    if (m_windows.empty()) {
        return std::weak_ptr<win::TabBoxClient>();
    }
    return m_windows.front();
}

bool MockTabBoxHandler::is_in_focus_chain(win::TabBoxClient* client) const
{
    if (!client) {
        return false;
    }
    for (auto const& window : m_windows) {
        if (window.get() == client) {
            return true;
        }
    }
    return false;
}

std::weak_ptr<win::TabBoxClient> MockTabBoxHandler::createMockWindow(const QString& caption)
{
    auto client = std::shared_ptr<win::TabBoxClient>{new MockTabBoxClient(caption)};
    m_windows.push_back(client);
    m_activeClient = client;
    return std::weak_ptr<win::TabBoxClient>(client);
}

void MockTabBoxHandler::closeWindow(win::TabBoxClient* client)
{
    auto it = m_windows.begin();
    for (; it != m_windows.end(); ++it) {
        if ((*it).get() == client) {
            m_windows.erase(it);
            return;
        }
    }
}

} // namespace KWin
