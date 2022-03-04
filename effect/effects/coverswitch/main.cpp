/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "coverswitch.h"

#include <kwineffects/effect_plugin_factory.h>

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(CoverSwitchEffect,
                              "metadata.json",
                              return CoverSwitchEffect::supported();)

}

#include "main.moc"
