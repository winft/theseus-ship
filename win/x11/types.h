/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::win::x11
{

/**
 * @brief Defines predicate matches on how to search for a window.
 */
enum class predicate_match {
    window,
    wrapper_id,
    frame_id,
    input_id,
};

}
