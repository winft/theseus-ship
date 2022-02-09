/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "property.h"

namespace KWin::base::x11::xcb
{

class motif_hints
{
public:
    motif_hints(xcb_atom_t atom)
        : m_atom(atom)
    {
    }
    void init(xcb_window_t window)
    {
        Q_ASSERT(window);
        if (m_window) {
            // already initialized
            return;
        }
        m_window = window;
        fetch();
    }
    void fetch()
    {
        if (!m_window) {
            return;
        }
        m_hints = nullptr;
        m_prop = property(0, m_window, m_atom, m_atom, 0, 5);
    }
    void read()
    {
        m_hints = m_prop.value<mwm_hints*>(32, m_atom, nullptr);
    }
    bool has_decoration() const
    {
        if (!m_window || !m_hints) {
            return false;
        }
        return m_hints->flags & uint32_t(Hints::Decorations);
    }
    bool no_border() const
    {
        if (!has_decoration()) {
            return false;
        }
        return !m_hints->decorations;
    }
    bool resize() const
    {
        return testFunction(Functions::Resize);
    }
    bool move() const
    {
        return testFunction(Functions::Move);
    }
    bool minimize() const
    {
        return testFunction(Functions::Minimize);
    }
    bool maximize() const
    {
        return testFunction(Functions::Maximize);
    }
    bool close() const
    {
        return testFunction(Functions::Close);
    }

private:
    struct mwm_hints {
        uint32_t flags;
        uint32_t functions;
        uint32_t decorations;
        int32_t input_mode;
        uint32_t status;
    };
    enum class Hints { Functions = (1L << 0), Decorations = (1L << 1) };
    enum class Functions {
        All = (1L << 0),
        Resize = (1L << 1),
        Move = (1L << 2),
        Minimize = (1L << 3),
        Maximize = (1L << 4),
        Close = (1L << 5)
    };
    bool testFunction(Functions flag) const
    {
        if (!m_window || !m_hints) {
            return true;
        }
        if (!(m_hints->flags & uint32_t(Hints::Functions))) {
            return true;
        }
        // if MWM_FUNC_ALL is set, other flags say what to turn _off_
        const bool set_value = ((m_hints->functions & uint32_t(Functions::All)) == 0);
        if (m_hints->functions & uint32_t(flag)) {
            return set_value;
        }
        return !set_value;
    }
    xcb_window_t m_window = XCB_WINDOW_NONE;
    property m_prop;
    xcb_atom_t m_atom;
    mwm_hints* m_hints = nullptr;
};

}
