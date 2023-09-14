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

win::tabbox_client* MockTabBoxHandler::active_client() const
{
    return m_activeClient;
}

void MockTabBoxHandler::set_active_client(win::tabbox_client* client)
{
    m_activeClient = client;
}

win::tabbox_client* MockTabBoxHandler::client_to_add_to_list(win::tabbox_client* client,
                                                             int desktop) const
{
    Q_UNUSED(desktop)
    auto const it = std::find_if(
        m_windows.begin(), m_windows.end(), [client](auto&& win) { return win.get() == client; });

    if (it != m_windows.end()) {
        return (*it).get();
    }

    return {};
}

win::tabbox_client* MockTabBoxHandler::next_client_focus_chain(win::tabbox_client* client) const
{
    auto it = m_windows.cbegin();
    for (; it != m_windows.cend(); ++it) {
        if ((*it).get() == client) {
            ++it;
            if (it == m_windows.cend()) {
                return m_windows.front().get();
            }
            return (*it).get();
        }
    }
    if (!m_windows.empty()) {
        return m_windows.back().get();
    }
    return {};
}

win::tabbox_client* MockTabBoxHandler::first_client_focus_chain() const
{
    if (m_windows.empty()) {
        return {};
    }
    return m_windows.front().get();
}

bool MockTabBoxHandler::is_in_focus_chain(win::tabbox_client* client) const
{
    if (!client) {
        return false;
    }
    auto const is_in_chain = std::any_of(
        m_windows.begin(), m_windows.end(), [client](auto&& win) { return win.get() == client; });

    return is_in_chain;
}

win::tabbox_client* MockTabBoxHandler::createMockWindow(const QString& caption)
{
    m_windows.emplace_back(std::make_unique<MockTabBoxClient>(caption));
    m_activeClient = m_windows.back().get();
    return m_windows.back().get();
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
