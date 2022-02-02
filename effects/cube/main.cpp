/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "cube.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(CubeEffect, "metadata.json", return CubeEffect::supported();)

}

#include "main.moc"
