/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Arjen Hiemstra <ahiemstra@heimr.nl>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "showfpseffect.h"

#include <render/effect/interface/effect_screen.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>

#include <QQmlContext>

namespace KWin
{

ShowFpsEffect::ShowFpsEffect()
{
}

ShowFpsEffect::~ShowFpsEffect() = default;

int ShowFpsEffect::fps() const
{
    return m_fps;
}

int ShowFpsEffect::maximumFps() const
{
    return m_maximumFps;
}

int ShowFpsEffect::paintDuration() const
{
    return m_paintDuration;
}

int ShowFpsEffect::paintAmount() const
{
    return m_paintAmount;
}

QColor ShowFpsEffect::paintColor() const
{
    auto normalizedDuration = std::min(1.0, m_paintDuration / 100.0);
    return QColor::fromHsvF(0.3 - (0.3 * normalizedDuration), 1.0, 1.0);
}

void ShowFpsEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    effects->prePaintScreen(data);

    m_newFps += 1;

    m_paintDurationTimer.restart();
    m_paintAmount = 0;

    // detect highest monitor refresh rate
    int maximumFps = 0;
    const auto screens = effects->screens();
    for (auto screen : screens) {
        maximumFps = std::max(screen->refreshRate(), maximumFps);
    }
    maximumFps /= 1000; // Convert from mHz to Hz.

    if (maximumFps != m_maximumFps) {
        m_maximumFps = maximumFps;
        Q_EMIT maximumFpsChanged();
    }

    if (!m_scene) {
        m_scene = std::make_unique<EffectQuickScene>();
        const auto url = QUrl::fromLocalFile(
            QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                   QStringLiteral("kwin/effects/showfps/qml/main.qml")));
        m_scene->setSource(url, {{QStringLiteral("effect"), QVariant::fromValue(this)}});
    }
}

void ShowFpsEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    auto now = std::chrono::steady_clock::now();
    if ((now - m_lastFpsTime) >= std::chrono::milliseconds(1000)) {
        m_fps = m_newFps;
        m_newFps = 0;
        m_lastFpsTime = now;
        Q_EMIT fpsChanged();
    }

    auto const rect = data.render.viewport;
    m_scene->setGeometry(QRect(rect.x() + rect.width() - 300, 0, 300, 150));
    effects->renderEffectQuickView(m_scene.get());
}

void ShowFpsEffect::paintWindow(effect::window_paint_data& data)
{
    effects->paintWindow(data);

    // Take intersection of region and actual window's rect, minus the fps area
    //  (since we keep repainting it) and count the pixels.
    auto repaintRegion = data.paint.region & data.window.frameGeometry();
    repaintRegion -= m_scene->geometry();
    for (const QRect& rect : repaintRegion) {
        m_paintAmount += rect.width() * rect.height();
    }
}

void ShowFpsEffect::postPaintScreen()
{
    effects->postPaintScreen();

    m_paintDuration = m_paintDurationTimer.elapsed();
    Q_EMIT paintChanged();

    effects->addRepaint(m_scene->geometry());
}

bool ShowFpsEffect::supported()
{
    return effects->isOpenGLCompositing();
}

} // namespace KWin
