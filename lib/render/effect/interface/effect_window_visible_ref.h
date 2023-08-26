/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/interface/effect_window.h>

namespace KWin
{

class EffectWindow;

/**
 * The EffectWindowVisibleRef provides a convenient way to force the visible status of a
 * window until an effect is finished animating it.
 */
class EffectWindowVisibleRef
{
public:
    EffectWindowVisibleRef()
        : m_window(nullptr)
        , m_reason(0)
    {
    }

    explicit EffectWindowVisibleRef(EffectWindow* window, int reason)
        : m_window(window)
        , m_reason(reason)
    {
        m_window->refVisible(this);
    }

    EffectWindowVisibleRef(EffectWindowVisibleRef const& other)
        : m_window(other.m_window)
        , m_reason(other.m_reason)
    {
        if (m_window) {
            m_window->refVisible(this);
        }
    }

    ~EffectWindowVisibleRef()
    {
        if (m_window) {
            m_window->unrefVisible(this);
        }
    }

    int reason() const
    {
        return m_reason;
    }

    EffectWindowVisibleRef& operator=(EffectWindowVisibleRef const& other)
    {
        if (other.m_window) {
            other.m_window->refVisible(&other);
        }
        if (m_window) {
            m_window->unrefVisible(this);
        }
        m_window = other.m_window;
        m_reason = other.m_reason;
        return *this;
    }

    bool isNull() const
    {
        return m_window == nullptr;
    }

private:
    EffectWindow* m_window;
    int m_reason;
};

}
