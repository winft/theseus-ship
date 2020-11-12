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
    Toplevel* m_lead{nullptr};
    std::vector<Toplevel*> m_children;
    bool m_modal{false};

    Toplevel* m_window;

public:
    explicit transient(Toplevel* win);
    virtual ~transient() = default;

    Toplevel* lead() const;
    void set_lead(Toplevel* lead);

    std::vector<Toplevel*> const& children() const;

    virtual bool has_child(Toplevel const* window, bool indirect) const;
    virtual void add_child(Toplevel* window);
    virtual void remove_child(Toplevel* window);
    void remove_child_nocheck(Toplevel* window);

    bool modal() const;
    void set_modal(bool modal);
};

}
}
