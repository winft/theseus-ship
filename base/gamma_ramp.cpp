/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "gamma_ramp.h"

namespace KWin::base
{

gamma_ramp::gamma_ramp(uint32_t size)
    : m_table(3 * size)
    , m_size(size)
{
}

uint32_t gamma_ramp::size() const
{
    return m_size;
}

uint16_t* gamma_ramp::red()
{
    return m_table.data();
}

uint16_t const* gamma_ramp::red() const
{
    return m_table.data();
}

uint16_t* gamma_ramp::green()
{
    return m_table.data() + m_size;
}

uint16_t const* gamma_ramp::green() const
{
    return m_table.data() + m_size;
}

uint16_t* gamma_ramp::blue()
{
    return m_table.data() + 2 * m_size;
}

uint16_t const* gamma_ramp::blue() const
{
    return m_table.data() + 2 * m_size;
}

}
