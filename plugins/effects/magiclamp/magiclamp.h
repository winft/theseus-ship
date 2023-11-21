/*
    SPDX-FileCopyrightText: 2008 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAGICLAMP_H
#define KWIN_MAGICLAMP_H

#include <render/effect/interface/effect_window_visible_ref.h>
#include <render/effect/interface/offscreen_effect.h>
#include <render/effect/interface/time_line.h>

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
    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void prePaintWindow(effect::window_prepaint_data& data) override;
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
    void slotWindowAdded(KWin::EffectWindow* w);
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
