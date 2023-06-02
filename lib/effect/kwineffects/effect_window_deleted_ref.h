/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect_window.h>

namespace KWin
{

class EffectWindow;

/**
 * The EffectWindowDeletedRef provides a convenient way to prevent deleting a closed
 * window until an effect has finished animating it.
 */
class EffectWindowDeletedRef
{
public:
    EffectWindowDeletedRef()
        : m_window(nullptr)
    {
    }

    explicit EffectWindowDeletedRef(EffectWindow* window)
        : m_window(window)
    {
        m_window->refWindow();
    }

    EffectWindowDeletedRef(EffectWindowDeletedRef const& other)
        : m_window(other.m_window)
    {
        if (m_window) {
            m_window->refWindow();
        }
    }

    ~EffectWindowDeletedRef()
    {
        if (m_window) {
            m_window->unrefWindow();
        }
    }

    EffectWindowDeletedRef& operator=(EffectWindowDeletedRef const& other)
    {
        if (other.m_window) {
            other.m_window->refWindow();
        }
        if (m_window) {
            m_window->unrefWindow();
        }
        m_window = other.m_window;
        return *this;
    }

    bool isNull() const
    {
        return m_window == nullptr;
    }

private:
    EffectWindow* m_window;
};

}
