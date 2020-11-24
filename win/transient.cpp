/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "transient.h"

#include "toplevel.h"

#include <cassert>

namespace KWin::win
{

transient::transient(Toplevel* win)
    : m_window{win}
{
}

transient::~transient()
{
    for (auto const& lead : m_leads) {
        remove_all(lead->transient()->m_children, m_window);
    }
    m_leads.clear();

    for (auto const& child : children()) {
        remove_child(child);
    }
}

Toplevel* transient::lead() const
{
    if (m_leads.size() == 0) {
        return nullptr;
    }
    return m_leads.front();
}

std::vector<Toplevel*> const& transient::leads() const
{
    return m_leads;
}

void transient::add_lead(Toplevel* lead)
{
    assert(m_window != lead);
    assert(!contains(m_leads, lead));

    m_leads.push_back(lead);
    Q_EMIT m_window->transientChanged();
}

void transient::remove_lead(Toplevel* lead)
{
    if (!contains(m_leads, lead)) {
        return;
    }

    remove_all(m_leads, lead);
    Q_EMIT m_window->transientChanged();
}

std::vector<Toplevel*> const& transient::children() const
{
    return m_children;
}

bool transient::has_child(Toplevel const* window, [[maybe_unused]] bool indirect) const
{
    return contains(m_children, window);
}

void transient::add_child(Toplevel* window)
{
    assert(m_window != window);

    if (contains(m_children, window)) {
        return;
    }

    m_children.push_back(window);
    window->transient()->add_lead(m_window);
}

void transient::remove_child(Toplevel* window)
{
    remove_all(m_children, window);
    window->transient()->remove_lead(m_window);
}

bool transient::is_follower_of(Toplevel* window)
{
    for (auto const& child : window->transient()->children()) {
        if (child == m_window) {
            return true;
        }
    }

    for (auto const& lead : m_leads) {
        if (lead->transient()->is_follower_of(window)) {
            return true;
        }
    }
    return false;
}

bool transient::modal() const
{
    return m_modal;
}

void transient::set_modal(bool modal)
{
    if (m_modal == modal) {
        return;
    }
    m_modal = modal;
    Q_EMIT m_window->modalChanged();
}

}
