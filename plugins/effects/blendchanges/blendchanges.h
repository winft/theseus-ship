/*
    SPDX-FileCopyrightText: 2022 David Edmundson <davidedmundson@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/interface/offscreen_effect.h>
#include <render/effect/interface/time_line.h>

#include <chrono>

namespace KWin
{

class BlendChanges : public OffscreenEffect
{
    Q_OBJECT

public:
    BlendChanges();
    ~BlendChanges() override;

    static bool supported();

    // Effect interface
    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    void drawWindow(effect::window_paint_data& data) override;
    void apply(effect::window_paint_data& data, WindowQuadList& quads) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 80;
    }

public Q_SLOTS:
    /**
     * Called from DBus, this should be called before triggering any changes
     * delay (ms) refers to how long to keep the current frame before starting a crossfade
     * We should expect all clients to have repainted by the time this expires
     */
    void start(int delay = 300);

private:
    TimeLine m_timeline;
    enum State {
        Off,
        ShowingCache,
        Blending,
    };
    State m_state = Off;
};

} // namespace KWin
