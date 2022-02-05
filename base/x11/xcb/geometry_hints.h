/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "property.h"

#include <QSize>

class TestXcbSizeHints;

namespace KWin::base::x11::xcb
{

class geometry_hints
{
public:
    geometry_hints() = default;
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
        m_sizeHints = nullptr;
        m_hints = normal_hints(m_window);
    }
    void read()
    {
        m_sizeHints = m_hints.sizeHints();
    }

    bool has_position() const
    {
        return test_flag(normal_hints::size_hints::user_position)
            || test_flag(normal_hints::size_hints::program_position);
    }
    bool has_size() const
    {
        return test_flag(normal_hints::size_hints::user_size)
            || test_flag(normal_hints::size_hints::program_size);
    }
    bool has_min_size() const
    {
        return test_flag(normal_hints::size_hints::min_size);
    }
    bool has_max_size() const
    {
        return test_flag(normal_hints::size_hints::max_size);
    }
    bool has_resize_increments() const
    {
        return test_flag(normal_hints::size_hints::resize_increments);
    }
    bool has_aspect() const
    {
        return test_flag(normal_hints::size_hints::aspect);
    }
    bool has_base_size() const
    {
        return test_flag(normal_hints::size_hints::base_size);
    }
    bool has_window_gravity() const
    {
        return test_flag(normal_hints::size_hints::window_gravity);
    }
    QSize max_size() const
    {
        if (!has_max_size()) {
            return QSize(INT_MAX, INT_MAX);
        }
        return QSize(qMax(m_sizeHints->maxWidth, 1), qMax(m_sizeHints->maxHeight, 1));
    }
    QSize min_size() const
    {
        if (!has_min_size()) {
            // according to ICCCM 4.1.23 base size should be used as a fallback
            return base_size();
        }
        return QSize(m_sizeHints->minWidth, m_sizeHints->minHeight);
    }
    QSize base_size() const
    {
        // Note: not using min_size as fallback
        if (!has_base_size()) {
            return QSize(0, 0);
        }
        return QSize(m_sizeHints->baseWidth, m_sizeHints->baseHeight);
    }
    QSize resize_increments() const
    {
        if (!has_resize_increments()) {
            return QSize(1, 1);
        }
        return QSize(qMax(m_sizeHints->widthInc, 1), qMax(m_sizeHints->heightInc, 1));
    }
    xcb_gravity_t window_gravity() const
    {
        if (!has_window_gravity()) {
            return XCB_GRAVITY_NORTH_WEST;
        }
        return xcb_gravity_t(m_sizeHints->winGravity);
    }
    QSize min_aspect() const
    {
        if (!has_aspect()) {
            return QSize(1, INT_MAX);
        }
        // prevent division by zero
        return QSize(m_sizeHints->minAspect[0], qMax(m_sizeHints->minAspect[1], 1));
    }
    QSize max_aspect() const
    {
        if (!has_aspect()) {
            return QSize(INT_MAX, 1);
        }
        // prevent division by zero
        return QSize(m_sizeHints->maxAspect[0], qMax(m_sizeHints->maxAspect[1], 1));
    }

private:
    /**
     * normal_hints as specified in ICCCM 4.1.2.3.
     */
    class normal_hints : public property
    {
    public:
        struct size_hints {
            enum Flags {
                user_position = 1,
                user_size = 2,
                program_position = 4,
                program_size = 8,
                min_size = 16,
                max_size = 32,
                resize_increments = 64,
                aspect = 128,
                base_size = 256,
                window_gravity = 512
            };
            qint32 flags = 0;
            qint32 pad[4] = {0, 0, 0, 0};
            qint32 minWidth = 0;
            qint32 minHeight = 0;
            qint32 maxWidth = 0;
            qint32 maxHeight = 0;
            qint32 widthInc = 0;
            qint32 heightInc = 0;
            qint32 minAspect[2] = {0, 0};
            qint32 maxAspect[2] = {0, 0};
            qint32 baseWidth = 0;
            qint32 baseHeight = 0;
            qint32 winGravity = 0;
        };
        explicit normal_hints()
            : property(){};
        explicit normal_hints(WindowId window)
            : property(0, window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0, 18)
        {
        }
        inline size_hints* sizeHints()
        {
            return value<size_hints*>(32, XCB_ATOM_WM_SIZE_HINTS, nullptr);
        }
    };

    friend TestXcbSizeHints;

    bool test_flag(normal_hints::size_hints::Flags flag) const
    {
        if (!m_window || !m_sizeHints) {
            return false;
        }
        return m_sizeHints->flags & flag;
    }

    xcb_window_t m_window = XCB_WINDOW_NONE;
    normal_hints m_hints;
    normal_hints::size_hints* m_sizeHints = nullptr;
};

}
