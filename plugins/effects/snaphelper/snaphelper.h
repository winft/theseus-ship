/*
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SNAPHELPER_H
#define KWIN_SNAPHELPER_H

#include <render/effect/interface/effect.h>
#include <render/effect/interface/time_line.h>

namespace KWin
{

class SnapHelperEffect : public Effect
{
    Q_OBJECT

public:
    SnapHelperEffect();
    ~SnapHelperEffect() override;

    void reconfigure(ReconfigureFlags flags) override;

    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void paintScreen(effect::screen_paint_data& data) override;
    void postPaintScreen() override;

    bool isActive() const override;

private Q_SLOTS:
    void slotWindowAdded(EffectWindow* w);
    void slotWindowClosed(EffectWindow* w);
    void slotWindowStartUserMovedResized(EffectWindow* w);
    void slotWindowFinishUserMovedResized(EffectWindow* w);
    void slotWindowFrameGeometryChanged(EffectWindow* w, const QRect& old);

private:
    QRect m_geometry;
    EffectWindow* m_window = nullptr;

    struct Animation {
        bool active = false;
        TimeLine timeLine;
    };

    Animation m_animation;
};

} // namespace KWin

#endif
