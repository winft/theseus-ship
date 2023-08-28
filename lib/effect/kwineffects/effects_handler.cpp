/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects_handler.h"

namespace KWin
{

EffectsHandler* effects{nullptr};

EffectsHandler::EffectsHandler()
{
    KWin::effects = this;
}

EffectsHandler::~EffectsHandler()
{
    // All effects should already be unloaded by Impl dtor
    Q_ASSERT(loaded_effects.count() == 0);
    KWin::effects = nullptr;
}

EffectWindow* EffectsHandler::findWindow(WId id) const
{
    return find_window_by_wid(id);
}

EffectWindow* EffectsHandler::findWindow(Wrapland::Server::Surface* surface) const
{
    return find_window_by_surface(surface);
}

EffectWindow* EffectsHandler::findWindow(QWindow* window) const
{
    return find_window_by_qwindow(window);
}

EffectWindow* EffectsHandler::findWindow(QUuid const& id) const
{
    return find_window_by_uuid(id);
}

}
