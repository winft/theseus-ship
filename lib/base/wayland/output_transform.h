/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::base::wayland
{

enum class output_transform {
    normal,
    rotated_90,
    rotated_180,
    rotated_270,
    flipped,
    flipped_90,
    flipped_180,
    flipped_270,
};

}
