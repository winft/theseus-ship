/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "transient.h"

#include "geo.h"
#include "remnant.h"
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
    auto top_lead = lead_of_annexed_transient(m_window);

    for (auto const& lead : m_leads) {
        remove_all(lead->transient()->children, m_window);
        if (annexed) {
            assert(top_lead);
            top_lead->discard_quads();
            top_lead->addLayerRepaint(visible_rect(m_window, m_window->frameGeometry()));
        }
    }
    m_leads.clear();

    auto const children_copy = children;
    for (auto const& child : children_copy) {
        if (annexed && top_lead) {
            top_lead->discard_quads();
            top_lead->addLayerRepaint(visible_rect(child, child->frameGeometry()));
        }
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

    if (m_window->remnant() && annexed) {
        m_window->remnant()->ref();
    }
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

    if (m_window->remnant() && annexed) {
        m_window->remnant()->unref();
    }
}

void transient::add_child(Toplevel* window)
{
    assert(m_window != window);

    // TODO(romangg): Instead of a silent fail we should add an assert. Consumers then must ensure
    //                to add a child only once. The X11 code needs to be adapted for that though.
    if (contains(children, window)) {
        return;
    }

    children.push_back(window);
    window->transient()->add_lead(m_window);

    if (window->transient()->annexed) {
        m_window->discard_quads();
    }
}

void transient::remove_child(Toplevel* window)
{
    remove_all(children, window);
    window->transient()->remove_lead(m_window);

    if (window->transient()->annexed) {
        // Need to check that a top-lead exists since this might be called on destroy of a lead.
        if (auto top_lead = lead_of_annexed_transient(m_window)) {
            top_lead->discard_quads();
            top_lead->addLayerRepaint(visible_rect(window, window->frameGeometry()));
        }
    }
}

bool transient::is_follower_of(Toplevel const* window)
{
    for (auto const& child : window->transient()->children) {
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
