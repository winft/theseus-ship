/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QMatrix4x4>

namespace KWin::render
{

inline QMatrix4x4 get_contrast_color_matrix(double contrast, double intensity, double saturation)
{
    QMatrix4x4 msatur;
    QMatrix4x4 mintens;
    QMatrix4x4 mcontr;

    // Saturation matrix
    if (!qFuzzyCompare(saturation, 1.0)) {
        auto const rval = (1.0 - saturation) * .2126;
        auto const gval = (1.0 - saturation) * .7152;
        auto const bval = (1.0 - saturation) * .0722;

        // clang-format off
        msatur = QMatrix4x4(rval + saturation, rval,              rval,              0.0,
                               gval,              gval + saturation, gval,              0.0,
                               bval,              bval,              bval + saturation, 0.0,
                               0,                 0,                 0,                 1.0);
        // clang-format on
    }

    // Intensity matrix
    if (!qFuzzyCompare(intensity, 1.0)) {
        mintens.scale(intensity, intensity, intensity);
    }

    // Contrast matrix
    if (!qFuzzyCompare(contrast, 1.0)) {
        auto const transl = (1.0 - contrast) / 2.0;

        // clang-format off
        mcontr = QMatrix4x4(contrast, 0,        0,        0.0,
                                0,        contrast, 0,        0.0,
                                0,        0,        contrast, 0.0,
                                transl,   transl,   transl,   1.0);
        // clang-format on
    }

    return mcontr * msatur * mintens;
}

}
