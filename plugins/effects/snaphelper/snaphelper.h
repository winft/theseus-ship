/*
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SNAPHELPER_H
#define KWIN_SNAPHELPER_H

#include <kwineffects/effect.h>
#include <kwineffects/time_line.h>

namespace KWin
{

class SnapHelperEffect : public Effect
{
    Q_OBJECT

public:
    SnapHelperEffect();
    ~SnapHelperEffect() override;

    void reconfigure(ReconfigureFlags flags) override;

    void prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(effect::screen_paint_data& data) override;
    void postPaintScreen() override;

    bool isActive() const override;

private Q_SLOTS:
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
