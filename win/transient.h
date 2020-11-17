/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils.h"
#include <kwin_export.h>

#include <vector>

namespace KWin
{
class Toplevel;

namespace win
{

template<typename Win>
Win* lead_of_annexed_transient(Win* win)
{
    if (win->transient()->annexed) {
        return lead_of_annexed_transient(win->transient()->lead());
    }
    return win;
}

class KWIN_EXPORT transient
{
private:
    std::vector<Toplevel*> m_leads;
    std::vector<Toplevel*> m_children;
    bool m_modal{false};

    Toplevel* m_window;

    void add_lead(Toplevel* lead);
    void remove_lead(Toplevel* lead);

public:
    explicit transient(Toplevel* win);
    virtual ~transient();

    /**
     * The transient lead at first position or nullptr when not a child.
     */
    Toplevel* lead() const;

    std::vector<Toplevel*> const& leads() const;
    std::vector<Toplevel*> const& children() const;

    virtual bool has_child(Toplevel const* window, bool indirect) const;
    virtual void add_child(Toplevel* window);
    virtual void remove_child(Toplevel* window);

    /**
     * Returns true when window is a lead for this directly or through a chain of leads indirectly.
     */
    bool is_follower_of(Toplevel* window);

    bool modal() const;
    void set_modal(bool modal);
};

}
}
