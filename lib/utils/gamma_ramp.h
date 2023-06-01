/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: MIT
*/
#pragma once

#include <cstdint>
#include <vector>

namespace KWin
{

class gamma_ramp
{
public:
    gamma_ramp(uint32_t size)
        : m_table(3 * size)
        , m_size(size)
    {
    }

    /**
     * Returns the size of the gamma ramp.
     */
    uint32_t size() const
    {
        return m_size;
    }

    /**
     * Returns pointer to the first red component in the gamma ramp.
     *
     * The returned pointer can be used for altering the red component
     * in the gamma ramp.
     */
    uint16_t* red()
    {
        return m_table.data();
    }

    /**
     * Returns pointer to the first red component in the gamma ramp.
     */
    uint16_t const* red() const
    {
        return m_table.data();
    }

    /**
     * Returns pointer to the first green component in the gamma ramp.
     *
     * The returned pointer can be used for altering the green component
     * in the gamma ramp.
     */
    uint16_t* green()
    {
        return m_table.data() + m_size;
    }

    /**
     * Returns pointer to the first green component in the gamma ramp.
     */
    uint16_t const* green() const
    {
        return m_table.data() + m_size;
    }

    /**
     * Returns pointer to the first blue component in the gamma ramp.
     *
     * The returned pointer can be used for altering the blue component
     * in the gamma ramp.
     */
    uint16_t* blue()
    {
        return m_table.data() + 2 * m_size;
    }

    /**
     * Returns pointer to the first blue component in the gamma ramp.
     */
    uint16_t const* blue() const
    {
        return m_table.data() + 2 * m_size;
    }

private:
    std::vector<uint16_t> m_table;
    uint32_t m_size;
};

}
