/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "coverswitch.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(CoverSwitchEffectFactory,
                              CoverSwitchEffect,
                              "metadata.json",
                              return CoverSwitchEffect::supported();)

}

#include "main.moc"
