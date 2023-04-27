/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects_handler.h"

namespace KWin
{

EffectsHandler* effects{nullptr};

EffectsHandler::EffectsHandler(CompositingType type)
    : compositing_type(type)
{
    if (compositing_type == NoCompositing)
        return;
    KWin::effects = this;
}

EffectsHandler::~EffectsHandler()
{
    // All effects should already be unloaded by Impl dtor
    Q_ASSERT(loaded_effects.count() == 0);
    KWin::effects = nullptr;
}

CompositingType EffectsHandler::compositingType() const
{
    return compositing_type;
}

bool EffectsHandler::isOpenGLCompositing() const
{
    return compositing_type == OpenGLCompositing;
}

QRect EffectsHandler::mapToRenderTarget(QRect const& rect) const
{
    auto const targetRect = renderTargetRect();
    auto const targetScale = renderTargetScale();

    return QRect((rect.x() - targetRect.x()) * targetScale,
                 (rect.y() - targetRect.y()) * targetScale,
                 rect.width() * targetScale,
                 rect.height() * targetScale);
}

QRegion EffectsHandler::mapToRenderTarget(QRegion const& region) const
{
    QRegion result;
    for (auto const& rect : region) {
        result += mapToRenderTarget(rect);
    }
    return result;
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

#include "moc_kwinglobals.cpp"
