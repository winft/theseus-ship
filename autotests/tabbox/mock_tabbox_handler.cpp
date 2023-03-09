/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "mock_tabbox_handler.h"

#include "mock_tabbox_client.h"

namespace KWin
{

MockTabBoxHandler::MockTabBoxHandler(QObject* parent)
    : tabbox_handler(nullptr, nullptr, parent)
{
}

MockTabBoxHandler::~MockTabBoxHandler()
{
}

void MockTabBoxHandler::grabbed_key_event(QKeyEvent* event) const
{
    Q_UNUSED(event)
}

std::weak_ptr<win::tabbox_client> MockTabBoxHandler::active_client() const
{
    return m_activeClient;
}

void MockTabBoxHandler::set_active_client(const std::weak_ptr<win::tabbox_client>& client)
{
    m_activeClient = client;
}

std::weak_ptr<win::tabbox_client>
MockTabBoxHandler::client_to_add_to_list(win::tabbox_client* client, int desktop) const
{
    Q_UNUSED(desktop)
    auto const window = std::find_if(
        m_windows.begin(), m_windows.end(), [client](auto win) { return win.get() == client; });

    if (window != m_windows.end()) {
        return *window;
    }

    return std::weak_ptr<win::tabbox_client>();
}

std::weak_ptr<win::tabbox_client>
MockTabBoxHandler::next_client_focus_chain(win::tabbox_client* client) const
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
    return std::weak_ptr<win::tabbox_client>();
}

std::weak_ptr<win::tabbox_client> MockTabBoxHandler::first_client_focus_chain() const
{
    if (m_windows.empty()) {
        return std::weak_ptr<win::tabbox_client>();
    }
    return m_windows.front();
}

bool MockTabBoxHandler::is_in_focus_chain(win::tabbox_client* client) const
{
    if (!client) {
        return false;
    }
    auto const is_in_chain = std::any_of(
        m_windows.begin(), m_windows.end(), [client](auto win) { return win.get() == client; });

    return is_in_chain;
}

std::weak_ptr<win::tabbox_client> MockTabBoxHandler::createMockWindow(const QString& caption)
{
    auto client = std::shared_ptr<win::tabbox_client>{new MockTabBoxClient(caption)};
    m_windows.push_back(client);
    m_activeClient = client;
    return std::weak_ptr<win::tabbox_client>(client);
}

void MockTabBoxHandler::closeWindow(win::tabbox_client* client)
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
