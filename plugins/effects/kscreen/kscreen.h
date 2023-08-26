/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_KSCREEN_H
#define KWIN_KSCREEN_H

#include <render/effect/interface/effect.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/paint_data.h>
#include <render/effect/interface/time_line.h>

namespace KWin
{

class KscreenEffect : public Effect
{
    Q_OBJECT

public:
    KscreenEffect();
    ~KscreenEffect() override;

    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(effect::window_paint_data& data) override;

    void reconfigure(ReconfigureFlags flags) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 99;
    }

    enum FadeOutState {
        StateNormal,
        StateFadingOut,
        StateFadedOut,
        StateFadingIn,
    };

    FadeOutState m_state{StateNormal};
    TimeLine m_timeLine;

private:
    void switchState();
};

} // namespace KWin
#endif // KWIN_KSCREEN_H
