/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "utils/algorithm.h"

#include <vector>

namespace KWin
{
class Toplevel;

namespace win
{

template<typename Win>
Toplevel* get_top_lead(Win* win)
{
    if (auto lead = win->transient()->lead()) {
        return get_top_lead(lead);
    }
    return win;
}

template<typename Win>
std::vector<Toplevel*> get_transient_descendants(Win* win)
{
    std::vector<Toplevel*> descendants;

    for (auto child : win->transient()->children) {
        descendants.push_back(child);
    }

    for (auto child : win->transient()->children) {
        auto const child_desc = get_transient_descendants(child);
        descendants.insert(descendants.end(), child_desc.begin(), child_desc.end());
    }

    return descendants;
}

template<typename Win>
std::vector<Toplevel*> get_transient_family(Win* win)
{
    auto top_lead = get_top_lead(win);
    auto relatives = get_transient_descendants(top_lead);
    relatives.push_back(top_lead);
    return relatives;
}

template<typename Win>
Toplevel* lead_of_annexed_transient(Win* win)
{
    if (win && win->transient()->annexed) {
        return lead_of_annexed_transient(win->transient()->lead());
    }
    return win;
}

class KWIN_EXPORT transient
{
private:
    std::vector<Toplevel*> m_leads;
    bool m_modal{false};

    Toplevel* m_window;

    void add_lead(Toplevel* lead);

public:
    std::vector<Toplevel*> children;
    bool annexed{false};
    bool input_grab{false};

    explicit transient(Toplevel* win);
    virtual ~transient();

    /**
     * The transient lead at first position or nullptr when not a child.
     */
    Toplevel* lead() const;
    std::vector<Toplevel*> const& leads() const;

    void add_child(Toplevel* window);
    void remove_child(Toplevel* window);

    /**
     * Returns true when window is a lead for this directly or through a chain of leads indirectly.
     */
    bool is_follower_of(Toplevel const* window);

    bool modal() const;
    void set_modal(bool modal);

protected:
    virtual void remove_lead(Toplevel* lead);
};

}
}
