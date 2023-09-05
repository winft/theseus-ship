/*
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_CUBESLIDE_H
#define KWIN_CUBESLIDE_H

#include <render/effect/interface/effect.h>
#include <render/effect/interface/effect_window_visible_ref.h>
#include <render/gl/interface/utils.h>

#include <QQueue>
#include <QSet>
#include <QTimeLine>

namespace KWin
{
class CubeSlideEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int rotationDuration READ configuredRotationDuration)
    Q_PROPERTY(bool dontSlidePanels READ isDontSlidePanels)
    Q_PROPERTY(bool dontSlideStickyWindows READ isDontSlideStickyWindows)
    Q_PROPERTY(bool usePagerLayout READ isUsePagerLayout)
    Q_PROPERTY(bool useWindowMoving READ isUseWindowMoving)
public:
    CubeSlideEffect();
    ~CubeSlideEffect() override;
    void reconfigure(ReconfigureFlags) override;
    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void paintScreen(effect::screen_paint_data& data) override;
    void postPaintScreen() override;
    void prePaintWindow(effect::window_prepaint_data& data) override;
    void paintWindow(effect::window_paint_data& data) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 50;
    }

    static bool supported();

    // for properties
    int configuredRotationDuration() const
    {
        return rotationDuration;
    }
    bool isDontSlidePanels() const
    {
        return dontSlidePanels;
    }
    bool isDontSlideStickyWindows() const
    {
        return dontSlideStickyWindows;
    }
    bool isUsePagerLayout() const
    {
        return usePagerLayout;
    }
    bool isUseWindowMoving() const
    {
        return useWindowMoving;
    }
private Q_SLOTS:
    void slotWindowAdded(EffectWindow* w);
    void slotWindowDeleted(EffectWindow* w);

    void slotDesktopChanged(int old, int current, EffectWindow* w);
    void slotWindowStepUserMovedResized(KWin::EffectWindow* w);
    void slotWindowFinishUserMovedResized(KWin::EffectWindow* w);
    void slotNumberDesktopsChanged();

private:
    enum RotationDirection { Left, Right, Upwards, Downwards };
    void paintSlideCube(effect::screen_paint_data const& data);
    void windowMovingChanged(float progress, RotationDirection direction);

    bool shouldAnimate(const EffectWindow* w) const;
    void startAnimation();

    bool cube_painting;
    int front_desktop;
    int painting_desktop;
    int other_desktop;
    bool firstDesktop;
    bool stickyPainting;
    QHash<EffectWindow*, EffectWindowVisibleRef> staticWindows;
    QTimeLine timeLine;
    std::chrono::milliseconds lastPresentTime;
    QQueue<RotationDirection> slideRotations;
    bool dontSlidePanels;
    bool dontSlideStickyWindows;
    bool usePagerLayout;
    int rotationDuration;
    bool useWindowMoving;
    bool windowMoving;
    bool desktopChangedWhileMoving;
    double progressRestriction;
};
}

#endif
