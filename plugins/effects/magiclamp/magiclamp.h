/*
    SPDX-FileCopyrightText: 2008 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAGICLAMP_H
#define KWIN_MAGICLAMP_H

#include <kwineffects/effect_window_visible_ref.h>
#include <kwineffects/offscreen_effect.h>
#include <kwineffects/time_line.h>

namespace KWin
{

struct MagicLampAnimation {
    EffectWindowVisibleRef visibleRef;
    TimeLine timeLine;
};

class MagicLampEffect : public OffscreenEffect
{
    Q_OBJECT

public:
    MagicLampEffect();

    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void postPaintScreen() override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 50;
    }

    static bool supported();

protected:
    void apply(effect::window_paint_data& data, WindowQuadList& quads) override;

public Q_SLOTS:
    void slotWindowDeleted(KWin::EffectWindow* w);
    void slotWindowMinimized(KWin::EffectWindow* w);
    void slotWindowUnminimized(KWin::EffectWindow* w);

private:
    std::chrono::milliseconds m_duration;
    QHash<EffectWindow*, MagicLampAnimation> m_animations;

    enum IconPosition { Top, Bottom, Left, Right };
};

} // namespace

#endif
