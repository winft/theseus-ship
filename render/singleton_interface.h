/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <functional>

namespace KWin
{

class EffectsHandler;
class EffectWindow;

namespace render
{

class basic_thumbnail_item;
class compositor_qobject;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static render::compositor_qobject* compositor;
    static EffectsHandler* effects;
    static std::function<bool()> supports_surfaceless_context;
    static std::function<void(EffectWindow&, basic_thumbnail_item&)> register_thumbnail;
};

}
}
