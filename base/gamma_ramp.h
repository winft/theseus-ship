/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QVector>
#include <kwin_export.h>

namespace KWin::base
{

class KWIN_EXPORT gamma_ramp
{
public:
    gamma_ramp(uint32_t size);

    /**
     * Returns the size of the gamma ramp.
     */
    uint32_t size() const;

    /**
     * Returns pointer to the first red component in the gamma ramp.
     *
     * The returned pointer can be used for altering the red component
     * in the gamma ramp.
     */
    uint16_t* red();

    /**
     * Returns pointer to the first red component in the gamma ramp.
     */
    uint16_t const* red() const;

    /**
     * Returns pointer to the first green component in the gamma ramp.
     *
     * The returned pointer can be used for altering the green component
     * in the gamma ramp.
     */
    uint16_t* green();

    /**
     * Returns pointer to the first green component in the gamma ramp.
     */
    uint16_t const* green() const;

    /**
     * Returns pointer to the first blue component in the gamma ramp.
     *
     * The returned pointer can be used for altering the blue component
     * in the gamma ramp.
     */
    uint16_t* blue();

    /**
     * Returns pointer to the first blue component in the gamma ramp.
     */
    uint16_t const* blue() const;

private:
    QVector<uint16_t> m_table;
    uint32_t m_size;
};

}
