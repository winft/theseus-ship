/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Marco Martin notmart@gmail.com
Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef KWIN_SLIDINGPOPUPS_H
#define KWIN_SLIDINGPOPUPS_H

#include <kwineffects/effect.h>
#include <kwineffects/effect_integration.h>
#include <kwineffects/time_line.h>

#include <memory>

namespace KWin
{

class SlidingPopupsEffect : public Effect
{
    Q_OBJECT
public:
    SlidingPopupsEffect();
    ~SlidingPopupsEffect() override;

    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data) override;
    void postPaintWindow(EffectWindow* w) override;
    void reconfigure(ReconfigureFlags flags) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 40;
    }

    static bool supported();

    void slideIn(EffectWindow* w);
    void setupAnimData(EffectWindow* w);

    enum class AnimationKind { In, Out };

    struct Animation {
        AnimationKind kind;
        TimeLine timeLine;
        std::chrono::milliseconds lastPresentTime = std::chrono::milliseconds::zero();
    };
    QHash<EffectWindow*, Animation> m_animations;
    QHash<const EffectWindow*, effect::anim_update> m_animationsData;

    std::chrono::milliseconds m_slideInDuration;
    std::chrono::milliseconds m_slideOutDuration;
    int m_slideLength;

private Q_SLOTS:
    void slotWindowDeleted(EffectWindow* w);
    void slideOut(EffectWindow* w);
    void stopAnimations();
};

}

#endif
