/*
    SPDX-FileCopyrightText: 2009 Marco Martin <notmart@gmail.com>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect.h>
#include <kwineffects/effect_integration.h>
#include <kwineffects/effect_window_deleted_ref.h>
#include <kwineffects/effect_window_visible_ref.h>
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

    void prePaintWindow(effect::window_prepaint_data& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(effect::window_paint_data& data) override;
    void postPaintWindow(EffectWindow* win) override;
    void reconfigure(ReconfigureFlags flags) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 40;
    }

    static bool supported();

    void slide_in(EffectWindow* win);
    void slide_out(EffectWindow* win);

    enum class AnimationKind {
        In,
        Out,
    };

    struct Animation {
        EffectWindowDeletedRef deletedRef;
        EffectWindowVisibleRef visibleRef;
        AnimationKind kind;
        TimeLine timeline;
    };
    QHash<EffectWindow*, Animation> animations;
    QHash<EffectWindow const*, effect::anim_update> window_data;

    struct {
        std::chrono::milliseconds in;
        std::chrono::milliseconds out;
        int distance;
    } config;

private:
    void handle_window_deleted(EffectWindow* win);
    void stopAnimations();
};

}
