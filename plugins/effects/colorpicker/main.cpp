/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "colorpicker.h"

#include <render/effect/interface/effect_plugin_factory.h>

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(ColorPickerEffect,
                              "metadata.json.stripped",
                              return ColorPickerEffect::supported();)

} // namespace KWin

#include "main.moc"
