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

class KWIN_EXPORT transient
{
private:
    std::vector<Toplevel*> m_leads;
    std::vector<Toplevel*> m_children;
    bool m_modal{false};

    Toplevel* m_window;

public:
    explicit transient(Toplevel* win);
    virtual ~transient() = default;

    /**
     * The transient lead at first position or nullptr when not a child.
     */
    Toplevel* lead() const;

    std::vector<Toplevel*> const& leads() const;
    std::vector<Toplevel*> const& children() const;

    void add_lead(Toplevel* lead);
    void remove_lead(Toplevel* lead);

    virtual bool has_child(Toplevel const* window, bool indirect) const;
    virtual void add_child(Toplevel* window);
    virtual void remove_child(Toplevel* window);
    void remove_child_nocheck(Toplevel* window);

    bool modal() const;
    void set_modal(bool modal);
};

}
}
