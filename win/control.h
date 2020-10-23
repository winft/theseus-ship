/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

namespace KWin
{
class Toplevel;

namespace win
{

class KWIN_EXPORT control
{
    bool m_skip_taskbar{false};
    bool m_original_skip_taskbar{false};
    bool m_skip_pager{false};
    bool m_skip_switcher{false};

    Toplevel* m_win;

public:
    explicit control(Toplevel* win);
    virtual ~control() = default;

    bool skip_pager() const;
    virtual void set_skip_pager(bool set);

    bool skip_switcher() const;
    virtual void set_skip_switcher(bool set);

    bool skip_taskbar() const;
    virtual void set_skip_taskbar(bool set);

    bool original_skip_taskbar() const;
    void set_original_skip_taskbar(bool set);
};

}
}
