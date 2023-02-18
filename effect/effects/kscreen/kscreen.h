/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_KSCREEN_H
#define KWIN_KSCREEN_H

#include <kwineffects/effect.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/paint_data.h>
#include <kwineffects/time_line.h>

namespace KWin
{

class KscreenEffect : public Effect
{
    Q_OBJECT

public:
    KscreenEffect();
    ~KscreenEffect() override;

    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data) override;

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
