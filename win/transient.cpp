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

Toplevel* transient::lead() const
{
    return m_lead;
}

void transient::set_lead(Toplevel* lead)
{
    if (m_lead == lead) {
        return;
    }

    assert(m_window != lead);

    m_lead = lead;
    Q_EMIT m_window->transientChanged();
}

std::vector<Toplevel*> const& transient::children() const
{
    return m_children;
}

bool transient::has_child(Toplevel const* window, [[maybe_unused]] bool indirect) const
{
    return window->transient()->lead() == m_window;
}

void transient::add_child(Toplevel* window)
{
    assert(!contains(m_children, window));
    assert(m_window != window);
    m_children.push_back(window);
}

void transient::remove_child(Toplevel* window)
{
    remove_all(m_children, window);
    if (window->transient()->lead() == m_window) {
        window->transient()->set_lead(nullptr);
    }
}

void transient::remove_child_nocheck(Toplevel* window)
{
    remove_all(m_children, window);
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
