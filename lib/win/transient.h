/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "damage.h"
#include "scene.h"

#include "utils/algorithm.h"

#include <cassert>
#include <vector>

namespace KWin::win
{

template<typename Win>
Win* find_modal(Win& win)
{
    if constexpr (requires(Win win) { win.findModal(); }) {
        return win.findModal();
    }
    return nullptr;
}

template<typename Win>
auto is_group_transient(Win const& win)
{
    if constexpr (requires(Win win) { win.groupTransient(); }) {
        return win.groupTransient();
    }
    return false;
}

template<typename Win>
auto get_top_lead(Win* win) -> decltype(win->transient->lead())
{
    if (auto lead = win->transient->lead()) {
        return get_top_lead(lead);
    }
    return win;
}

template<typename Win>
auto get_transient_descendants(Win* win) -> decltype(win->transient->children)
{
    decltype(win->transient->children) descendants;

    for (auto child : win->transient->children) {
        descendants.push_back(child);
    }

    for (auto child : win->transient->children) {
        auto const child_desc = get_transient_descendants(child);
        descendants.insert(descendants.end(), child_desc.begin(), child_desc.end());
    }

    return descendants;
}

template<typename Win>
auto get_transient_family(Win* win)
{
    auto top_lead = get_top_lead(win);
    auto relatives = get_transient_descendants(top_lead);
    relatives.push_back(top_lead);
    return relatives;
}

template<typename Win>
auto lead_of_annexed_transient(Win* win) -> decltype(win->transient->lead())
{
    if (win && win->transient->annexed) {
        return lead_of_annexed_transient(win->transient->lead());
    }
    return win;
}

template<typename Window>
class transient
{
public:
    explicit transient(Window* win)
        : m_window{win}
    {
    }

    virtual ~transient()
    {
        auto top_lead = lead_of_annexed_transient(m_window);

        for (auto const& lead : m_leads) {
            remove_all(lead->transient->children, m_window);
            if (annexed) {
                assert(top_lead);
                discard_shape(*top_lead);
                add_layer_repaint(*top_lead, visible_rect(m_window, m_window->geo.frame));
            }
        }
        m_leads.clear();

        auto const children_copy = children;
        for (auto const& child : children_copy) {
            if (annexed && top_lead) {
                discard_shape(*top_lead);
                add_layer_repaint(*top_lead, visible_rect(child, child->geo.frame));
            }
            remove_child(child);
        }
    }

    /**
     * The transient lead at first position or nullptr when not a child.
     */
    Window* lead() const
    {
        if (m_leads.size() == 0) {
            return nullptr;
        }
        return m_leads.front();
    }

    std::vector<Window*> const& leads() const
    {
        return m_leads;
    }

    void add_child(Window* window)
    {
        assert(m_window != window);

        // TODO(romangg): Instead of a silent fail we should add an assert. Consumers then must
        //                ensure to add a child only once. X11 code must be adapted for that though.
        if (contains(children, window)) {
            return;
        }

        children.push_back(window);
        window->transient->add_lead(m_window);

        if (window->transient->annexed) {
            discard_shape(*m_window);
        }
    }

    void remove_child(Window* window)
    {
        assert(contains(children, window));
        remove_all(children, window);
        window->transient->remove_lead(m_window);

        if (window->transient->annexed) {
            // Need to check that a top-lead exists since this might be called on destroy of a lead.
            if (auto top_lead = lead_of_annexed_transient(m_window)) {
                discard_shape(*top_lead);
                add_layer_repaint(*top_lead, visible_rect(window, window->geo.frame));
            }
        }
    }

    /**
     * Returns true when window is a lead for this directly or through a chain of leads indirectly.
     */
    bool is_follower_of(Window const* window)
    {
        for (auto const& child : window->transient->children) {
            if (child == m_window) {
                return true;
            }
        }

        for (auto const& lead : m_leads) {
            if (lead->transient->is_follower_of(window)) {
                return true;
            }
        }
        return false;
    }

    bool modal() const
    {
        return m_modal;
    }

    void set_modal(bool modal)
    {
        if (m_modal == modal) {
            return;
        }
        m_modal = modal;
        Q_EMIT m_window->qobject->modalChanged();
    }

    std::vector<Window*> children;
    bool annexed{false};
    bool input_grab{false};

protected:
    virtual void remove_lead(Window* lead)
    {
        assert(contains(m_leads, lead));
        remove_all(m_leads, lead);
        Q_EMIT m_window->qobject->transientChanged();

        if (m_window->remnant && annexed) {
            m_window->remnant->unref();
        }
    }

private:
    void add_lead(Window* lead)
    {
        assert(m_window != lead);
        assert(!contains(m_leads, lead));

        if (m_window->remnant && annexed) {
            m_window->remnant->ref();
        }
        m_leads.push_back(lead);
        Q_EMIT m_window->qobject->transientChanged();
    }

    std::vector<Window*> m_leads;
    bool m_modal{false};

    Window* m_window;
};

}
