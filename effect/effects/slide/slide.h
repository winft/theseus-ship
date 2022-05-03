/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2007 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2008 Lucas Murray <lmurray@undefinedfire.com>
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
#ifndef KWIN_SLIDE_H
#define KWIN_SLIDE_H

#include "kwineffects/effect_screen.h"
#include <kwineffects/effect.h>
#include <kwineffects/time_line.h>
#include <kwineffects/types.h>

namespace KWin
{

/*
 * How it Works:
 *
 * This effect doesn't change the current desktop, only recieves changes from the
 * VirtualDesktopManager. The only visually aparent inputs are desktopChanged() and
 * desktopChanging().
 *
 * When responding to desktopChanging(), the draw position is only affected by what's recieved from
 * there. After desktopChanging() is done, or without desktopChanging() having been called at all,
 * desktopChanged() is called. The desktopChanged() function configures the m_startPos and m_endPos
 * for the animation, and the duration.
 *
 * m_currentPosition and m_paintCtx.translation and everything else not labeled "drawCoordinate"
 * uses desktops as a unit.
 * Exmp: 1.2 means the dekstop at index 1 shifted over by .2 desktops.
 * All coords must be positive.
 *
 * For the wrapping effect, the render loop has to handle desktop coordinates larger than the total
 * grid's width.
 * 1. It uses modulus to keep the desktop coords in the range [0, gridWidth].
 * 2. It will draw the desktop at index 0 at index gridWidth if it has to.
 * I will not draw any thing farther outside the range than that.
 *
 * I've put an explanation of all the important private vars down at the bottom.
 *
 * Good luck :)
 */

class SlideEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int duration READ duration)
    Q_PROPERTY(int horizontalGap READ horizontalGap)
    Q_PROPERTY(int verticalGap READ verticalGap)
    Q_PROPERTY(bool slideDocks READ slideDocks)
    Q_PROPERTY(bool slideBackground READ slideBackground)

public:
    SlideEffect();
    ~SlideEffect() override;

    void reconfigure(ReconfigureFlags) override;

    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion& region, ScreenPaintData& data) override;
    void postPaintScreen() override;

    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data) override;

    bool isActive() const override;
    int requestedEffectChainPosition() const override;

    static bool supported();

    int duration() const;
    int horizontalGap() const;
    int verticalGap() const;
    bool slideDocks() const;
    bool slideBackground() const;

private Q_SLOTS:
    void desktopChanged(int old, int current, EffectWindow* with);
    void desktopChanging(uint old, QPointF desktopOffset, EffectWindow* with);
    void desktopChangingCancelled();
    void windowAdded(EffectWindow* w);
    void windowDeleted(EffectWindow* w);

private:
    QPoint getDrawCoords(QPointF pos, EffectScreen* screen);
    bool isTranslated(const EffectWindow* w) const;
    bool isPainted(const EffectWindow* w) const;
    bool shouldElevate(const EffectWindow* w) const;
    QPointF moveInsideDesktopGrid(QPointF p);
    QPointF constrainToDrawableRange(QPointF p);
    QPointF forcePositivePosition(QPointF p) const;
    void optimizePath(); // Find the best path to target desktop

    void startAnimation(int old, int current, EffectWindow* movingWindow = nullptr);
    void finishedSwitching();

private:
    int m_hGap;
    int m_vGap;
    bool m_slideDocks;
    bool m_slideBackground;
    int m_fullAnimationDuration; // Miliseconds for 1 complete desktop switch

    enum class State {
        Inactive,
        ActiveAnimation,
        ActiveGesture,
    };

    State m_state = State::Inactive;
    TimeLine m_timeLine;

    // When the desktop isn't desktopChanging(), these two variables are used to control the
    // animation path. They use desktops as a unit.
    QPointF m_startPos;
    QPointF m_endPos;

    EffectWindow* m_movingWindow = nullptr;
    std::chrono::milliseconds m_lastPresentTime = std::chrono::milliseconds::zero();
    QPointF
        m_currentPosition; // Should always be kept up to date with where on the grid we're seeing.

    struct {
        int desktop;
        bool firstPass;
        bool lastPass;
        QPointF translation; // Uses desktops as units

        QPoint currentPos;
        QVector<int> visibleDesktops;
        EffectWindowList fullscreenWindows;
    } m_paintCtx;

    EffectWindowList m_elevatedWindows;
};

inline int SlideEffect::duration() const
{
    return m_fullAnimationDuration;
}

inline int SlideEffect::horizontalGap() const
{
    return m_hGap;
}

inline int SlideEffect::verticalGap() const
{
    return m_vGap;
}

inline bool SlideEffect::slideDocks() const
{
    return m_slideDocks;
}

inline bool SlideEffect::slideBackground() const
{
    return m_slideBackground;
}

inline bool SlideEffect::isActive() const
{
    return m_state != State::Inactive;
}

inline int SlideEffect::requestedEffectChainPosition() const
{
    return 50;
}

} // namespace KWin

#endif
