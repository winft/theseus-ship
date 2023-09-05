/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Arjen Hiemstra <ahiemstra@heimr.nl>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <render/effect/interface/effect.h>
#include <render/effect/interface/effect_quick_view.h>

#include <QElapsedTimer>

namespace KWin
{

class ShowFpsEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(int maximumFps READ maximumFps NOTIFY maximumFpsChanged)
    Q_PROPERTY(int paintDuration READ paintDuration NOTIFY paintChanged)
    Q_PROPERTY(int paintAmount READ paintAmount NOTIFY paintChanged)
    Q_PROPERTY(QColor paintColor READ paintColor NOTIFY paintChanged)

public:
    ShowFpsEffect();
    ~ShowFpsEffect() override;

    int fps() const;
    int maximumFps() const;
    int paintDuration() const;
    int paintAmount() const;
    QColor paintColor() const;

    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void paintScreen(effect::screen_paint_data& data) override;
    void paintWindow(effect::window_paint_data& data) override;
    void postPaintScreen() override;

    static bool supported();

Q_SIGNALS:
    void fpsChanged();
    void maximumFpsChanged();
    void paintChanged();

private:
    std::unique_ptr<EffectQuickScene> m_scene;

    int m_maximumFps = 0;

    int m_fps = 0;
    int m_newFps = 0;
    std::chrono::steady_clock::time_point m_lastFpsTime;

    int m_paintDuration = 0;
    int m_paintAmount = 0;
    QElapsedTimer m_paintDurationTimer;
};

} // namespace KWin
