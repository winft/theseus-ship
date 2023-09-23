/*
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "cubeslide.h"
// KConfigSkeleton
#include "cubeslideconfig.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/utils.h>

#include <QVector3D>

#include <cmath>

namespace KWin
{

CubeSlideEffect::CubeSlideEffect()
    : stickyPainting(false)
    , lastPresentTime(std::chrono::milliseconds::zero())
    , windowMoving(false)
    , desktopChangedWhileMoving(false)
    , progressRestriction(0.0f)
{
    initConfig<CubeSlideConfig>();
    connect(effects, &EffectsHandler::windowAdded, this, &CubeSlideEffect::slotWindowAdded);
    connect(effects, &EffectsHandler::windowDeleted, this, &CubeSlideEffect::slotWindowDeleted);
    connect(effects,
            QOverload<int, int, EffectWindow*>::of(&EffectsHandler::desktopChanged),
            this,
            &CubeSlideEffect::slotDesktopChanged);
    connect(effects,
            &EffectsHandler::numberDesktopsChanged,
            this,
            &CubeSlideEffect::slotNumberDesktopsChanged);
    reconfigure(ReconfigureAll);
}

CubeSlideEffect::~CubeSlideEffect()
{
}

bool CubeSlideEffect::supported()
{
    return effects->isOpenGLCompositing() && effects->animationsSupported();
}

void CubeSlideEffect::reconfigure(ReconfigureFlags)
{
    CubeSlideConfig::self()->read();
    // TODO: rename rotationDuration to duration
    rotationDuration = animationTime(
        CubeSlideConfig::rotationDuration() != 0 ? CubeSlideConfig::rotationDuration() : 500);
    timeLine.setEasingCurve(QEasingCurve::InOutSine);
    timeLine.setDuration(rotationDuration);
    dontSlidePanels = CubeSlideConfig::dontSlidePanels();
    dontSlideStickyWindows = CubeSlideConfig::dontSlideStickyWindows();
    usePagerLayout = CubeSlideConfig::usePagerLayout();
    useWindowMoving = CubeSlideConfig::useWindowMoving();
}

void CubeSlideEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    std::chrono::milliseconds delta = std::chrono::milliseconds::zero();
    if (lastPresentTime.count()) {
        delta = data.present_time - lastPresentTime;
    }
    lastPresentTime = data.present_time;

    if (isActive()) {
        data.paint.mask |= PAINT_SCREEN_TRANSFORMED | PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS
            | PAINT_SCREEN_BACKGROUND_FIRST;
        timeLine.setCurrentTime(timeLine.currentTime() + delta.count());
        if (windowMoving
            && timeLine.currentTime()
                > progressRestriction * static_cast<qreal>(timeLine.duration()))
            timeLine.setCurrentTime(progressRestriction * static_cast<qreal>(timeLine.duration()));
    }
    effects->prePaintScreen(data);
}

void CubeSlideEffect::paintScreen(effect::screen_paint_data& data)
{
    if (isActive()) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        paintSlideCube(data);
        glCullFace(GL_BACK);
        paintSlideCube(data);
        glDisable(GL_CULL_FACE);
        // Paint an extra screen with 'sticky' windows.
        if (!staticWindows.isEmpty()) {
            stickyPainting = true;
            effects->paintScreen(data);
            stickyPainting = false;
        }
    } else {
        effects->paintScreen(data);
    }
}

void CubeSlideEffect::paintSlideCube(effect::screen_paint_data const& data)
{
    // slide cube only paints to desktops at a time
    // first the horizontal rotations followed by vertical rotations
    auto rect = effects->clientArea(FullArea, effects->activeScreen(), effects->currentDesktop());
    float point = rect.width() / 2 * tan(45.0f * M_PI / 180.0f);
    cube_painting = true;
    painting_desktop = front_desktop;

    auto firstFaceData = data;
    auto secondFaceData = data;
    auto direction = slideRotations.head();
    int secondDesktop;

    QVector3D const x_axis{1, 0, 0};
    QVector3D const y_axis{0, 1, 0};

    switch (direction) {
    case Left:
        firstFaceData.paint.geo.rotation.axis = y_axis;
        secondFaceData.paint.geo.rotation.axis = y_axis;

        if (usePagerLayout) {
            secondDesktop = effects->desktopToLeft(front_desktop, true);
        } else {
            secondDesktop = front_desktop - 1;
            if (secondDesktop == 0)
                secondDesktop = effects->numberOfDesktops();
        }

        firstFaceData.paint.geo.rotation.angle = 90.0f * timeLine.currentValue();
        secondFaceData.paint.geo.rotation.angle = -90.0f * (1.0f - timeLine.currentValue());
        break;
    case Right:
        firstFaceData.paint.geo.rotation.axis = y_axis;
        secondFaceData.paint.geo.rotation.axis = y_axis;

        if (usePagerLayout) {
            secondDesktop = effects->desktopToRight(front_desktop, true);
        } else {
            secondDesktop = front_desktop + 1;
            if (secondDesktop > effects->numberOfDesktops())
                secondDesktop = 1;
        }

        firstFaceData.paint.geo.rotation.angle = -90.0f * timeLine.currentValue();
        secondFaceData.paint.geo.rotation.angle = 90.0f * (1.0f - timeLine.currentValue());
        break;
    case Upwards:
        firstFaceData.paint.geo.rotation.axis = x_axis;
        secondFaceData.paint.geo.rotation.axis = x_axis;
        secondDesktop = effects->desktopAbove(front_desktop, true);
        firstFaceData.paint.geo.rotation.angle = -90.0f * timeLine.currentValue();
        secondFaceData.paint.geo.rotation.angle = 90.0f * (1.0f - timeLine.currentValue());
        point = rect.height() / 2 * tan(45.0f * M_PI / 180.0f);
        break;
    case Downwards:
        firstFaceData.paint.geo.rotation.axis = x_axis;
        secondFaceData.paint.geo.rotation.axis = x_axis;
        secondDesktop = effects->desktopBelow(front_desktop, true);
        firstFaceData.paint.geo.rotation.angle = 90.0f * timeLine.currentValue();
        secondFaceData.paint.geo.rotation.angle = -90.0f * (1.0f - timeLine.currentValue());
        point = rect.height() / 2 * tan(45.0f * M_PI / 180.0f);
        break;
    default:
        // totally impossible
        return;
    }

    // front desktop
    firstFaceData.paint.geo.rotation.origin
        = QVector3D(rect.width() / 2, rect.height() / 2, -point);
    other_desktop = secondDesktop;
    firstDesktop = true;
    effects->paintScreen(firstFaceData);

    // second desktop
    other_desktop = painting_desktop;
    painting_desktop = secondDesktop;
    firstDesktop = false;
    secondFaceData.paint.geo.rotation.origin
        = QVector3D(rect.width() / 2, rect.height() / 2, -point);
    effects->paintScreen(secondFaceData);
    cube_painting = false;
    painting_desktop = effects->currentDesktop();
}

void CubeSlideEffect::prePaintWindow(effect::window_prepaint_data& data)
{
    if (isActive() && !stickyPainting && cube_painting) {
        if (staticWindows.contains(&data.window)) {
            effects->prePaintWindow(data);
            return;
        }

        auto rect = effects->clientArea(FullArea, effects->activeScreen(), painting_desktop);
        if (data.window.isOnDesktop(painting_desktop)) {
            if (data.window.x() < rect.x()) {
                data.quads = data.quads.splitAtX(-data.window.x());
            }
            if (data.window.x() + data.window.width() > rect.x() + rect.width()) {
                data.quads = data.quads.splitAtX(rect.width() - data.window.x());
            }
            if (data.window.y() < rect.y()) {
                data.quads = data.quads.splitAtY(-data.window.y());
            }
            if (data.window.y() + data.window.height() > rect.y() + rect.height()) {
                data.quads = data.quads.splitAtY(rect.height() - data.window.y());
            }
        } else if (data.window.isOnDesktop(other_desktop)) {
            RotationDirection direction = slideRotations.head();
            bool enable = false;
            if (data.window.x() < rect.x() && (direction == Left || direction == Right)) {
                data.quads = data.quads.splitAtX(-data.window.x());
                enable = true;
            }
            if (data.window.x() + data.window.width() > rect.x() + rect.width()
                && (direction == Left || direction == Right)) {
                data.quads = data.quads.splitAtX(rect.width() - data.window.x());
                enable = true;
            }
            if (data.window.y() < rect.y() && (direction == Upwards || direction == Downwards)) {
                data.quads = data.quads.splitAtY(-data.window.y());
                enable = true;
            }
            if (data.window.y() + data.window.height() > rect.y() + rect.height()
                && (direction == Upwards || direction == Downwards)) {
                data.quads = data.quads.splitAtY(rect.height() - data.window.y());
                enable = true;
            }
            if (enable) {
                data.paint.mask |= PAINT_WINDOW_TRANSFORMED;
                data.set_translucent();
            }
        }
    }

    effects->prePaintWindow(data);
}

void CubeSlideEffect::paintWindow(effect::window_paint_data& data)
{
    if (!isActive() || !cube_painting || staticWindows.contains(&data.window)) {
        effects->paintWindow(data);
        return;
    }

    // filter out quads overlapping the edges
    auto rect = effects->clientArea(FullArea, effects->activeScreen(), painting_desktop);

    if (data.window.isOnDesktop(painting_desktop)) {
        if (data.window.x() < rect.x()) {
            WindowQuadList new_quads;
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.right() > -data.window.x()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }
        if (data.window.x() + data.window.width() > rect.x() + rect.width()) {
            WindowQuadList new_quads;
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.right() <= rect.width() - data.window.x()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }
        if (data.window.y() < rect.y()) {
            WindowQuadList new_quads;
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.bottom() > -data.window.y()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }
        if (data.window.y() + data.window.height() > rect.y() + rect.height()) {
            WindowQuadList new_quads;
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.bottom() <= rect.height() - data.window.y()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }
    }

    // paint windows overlapping edges from other desktop
    if (data.window.isOnDesktop(other_desktop) && (data.paint.mask & PAINT_WINDOW_TRANSFORMED)) {
        auto direction = slideRotations.head();

        if (data.window.x() < rect.x() && (direction == Left || direction == Right)) {
            WindowQuadList new_quads;
            data.paint.geo.translation.setX(rect.width());
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.right() <= -data.window.x()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }

        if (data.window.x() + data.window.width() > rect.x() + rect.width()
            && (direction == Left || direction == Right)) {
            WindowQuadList new_quads;
            data.paint.geo.translation.setX(-rect.width());
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.right() > rect.width() - data.window.x()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }

        if (data.window.y() < rect.y() && (direction == Upwards || direction == Downwards)) {
            WindowQuadList new_quads;
            data.paint.geo.translation.setY(rect.height());
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.bottom() <= -data.window.y()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }

        if (data.window.y() + data.window.height() > rect.y() + rect.height()
            && (direction == Upwards || direction == Downwards)) {
            WindowQuadList new_quads;
            data.paint.geo.translation.setY(-rect.height());
            for (auto const& quad : qAsConst(data.quads)) {
                if (quad.bottom() > rect.height() - data.window.y()) {
                    new_quads.append(quad);
                }
            }
            data.quads = new_quads;
        }

        if (firstDesktop) {
            data.paint.opacity *= timeLine.currentValue();
        } else {
            data.paint.opacity *= 1.0 - timeLine.currentValue();
        }
    }

    effects->paintWindow(data);
}

void CubeSlideEffect::postPaintScreen()
{
    effects->postPaintScreen();

    if (!isActive()) {
        return;
    }

    if (timeLine.currentValue() == 1.0) {
        auto direction = slideRotations.dequeue();

        switch (direction) {
        case Left:
            if (usePagerLayout)
                front_desktop = effects->desktopToLeft(front_desktop, true);
            else {
                front_desktop--;
                if (front_desktop == 0)
                    front_desktop = effects->numberOfDesktops();
            }
            break;
        case Right:
            if (usePagerLayout)
                front_desktop = effects->desktopToRight(front_desktop, true);
            else {
                front_desktop++;
                if (front_desktop > effects->numberOfDesktops())
                    front_desktop = 1;
            }
            break;
        case Upwards:
            front_desktop = effects->desktopAbove(front_desktop, true);
            break;
        case Downwards:
            front_desktop = effects->desktopBelow(front_desktop, true);
            break;
        }

        timeLine.setCurrentTime(0);
        if (slideRotations.count() == 1) {
            timeLine.setEasingCurve(QEasingCurve::OutSine);
        } else {
            timeLine.setEasingCurve(QEasingCurve::Linear);
        }

        if (slideRotations.empty()) {
            auto const keys = staticWindows.keys();
            for (EffectWindow* w : qAsConst(keys)) {
                w->setData(WindowForceBlurRole, QVariant());
                w->setData(WindowForceBackgroundContrastRole, QVariant());
            }
            staticWindows.clear();
            lastPresentTime = std::chrono::milliseconds::zero();
            effects->setActiveFullScreenEffect(nullptr);
        }
    }

    effects->addRepaintFull();
}

void CubeSlideEffect::slotDesktopChanged(int old, int current, EffectWindow* w)
{
    Q_UNUSED(w)

    if (effects->activeFullScreenEffect() && effects->activeFullScreenEffect() != this) {
        return;
    }
    if (old > effects->numberOfDesktops()) {
        // number of desktops has been reduced -> no animation
        return;
    }
    if (windowMoving) {
        desktopChangedWhileMoving = true;
        progressRestriction = 1.0 - progressRestriction;
        effects->addRepaintFull();
        return;
    }

    auto activate = true;

    if (!slideRotations.empty()) {
        // last slide still in progress
        activate = false;
        auto direction = slideRotations.dequeue();
        slideRotations.clear();
        slideRotations.enqueue(direction);

        switch (direction) {
        case Left:
            if (usePagerLayout) {
                old = effects->desktopToLeft(front_desktop, true);
            } else {
                old = front_desktop - 1;
                if (old == 0) {
                    old = effects->numberOfDesktops();
                }
            }
            break;
        case Right:
            if (usePagerLayout) {
                old = effects->desktopToRight(front_desktop, true);
            } else {
                old = front_desktop + 1;
                if (old > effects->numberOfDesktops())
                    old = 1;
            }
            break;
        case Upwards:
            old = effects->desktopAbove(front_desktop, true);
            break;
        case Downwards:
            old = effects->desktopBelow(front_desktop, true);
            break;
        }
    }
    if (usePagerLayout) {
        // calculate distance in respect to pager
        auto diff = effects->desktopGridCoords(effects->currentDesktop())
            - effects->desktopGridCoords(old);

        if (qAbs(diff.x()) > effects->desktopGridWidth() / 2) {
            int sign = -1 * (diff.x() / qAbs(diff.x()));
            diff.setX(sign * (effects->desktopGridWidth() - qAbs(diff.x())));
        }

        if (diff.x() > 0) {
            for (int i = 0; i < diff.x(); i++) {
                slideRotations.enqueue(Right);
            }
        } else if (diff.x() < 0) {
            diff.setX(-diff.x());
            for (int i = 0; i < diff.x(); i++) {
                slideRotations.enqueue(Left);
            }
        }

        if (qAbs(diff.y()) > effects->desktopGridHeight() / 2) {
            int sign = -1 * (diff.y() / qAbs(diff.y()));
            diff.setY(sign * (effects->desktopGridHeight() - qAbs(diff.y())));
        }

        if (diff.y() > 0) {
            for (int i = 0; i < diff.y(); i++) {
                slideRotations.enqueue(Downwards);
            }
        }

        if (diff.y() < 0) {
            diff.setY(-diff.y());
            for (int i = 0; i < diff.y(); i++) {
                slideRotations.enqueue(Upwards);
            }
        }
    } else {
        // ignore pager layout
        auto left = old - current;
        if (left < 0) {
            left = effects->numberOfDesktops() + left;
        }

        auto right = current - old;
        if (right < 0) {
            right = effects->numberOfDesktops() + right;
        }

        if (left < right) {
            for (int i = 0; i < left; i++) {
                slideRotations.enqueue(Left);
            }
        } else {
            for (int i = 0; i < right; i++) {
                slideRotations.enqueue(Right);
            }
        }
    }

    timeLine.setDuration(static_cast<float>(rotationDuration)
                         / static_cast<float>(slideRotations.count()));

    if (activate) {
        startAnimation();
        front_desktop = old;
        effects->addRepaintFull();
    }
}

void CubeSlideEffect::startAnimation()
{
    auto const windows = effects->stackingOrder();

    for (auto w : windows) {
        if (!shouldAnimate(w)) {
            w->setData(WindowForceBlurRole, QVariant(true));
            w->setData(WindowForceBackgroundContrastRole, QVariant(true));
            staticWindows[w] = EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_DESKTOP);
        }
    }

    if (slideRotations.count() == 1) {
        timeLine.setEasingCurve(QEasingCurve::InOutSine);
    } else {
        timeLine.setEasingCurve(QEasingCurve::InSine);
    }

    effects->setActiveFullScreenEffect(this);
    timeLine.setCurrentTime(0);
}

void CubeSlideEffect::slotWindowAdded(EffectWindow* w)
{
    connect(w,
            &EffectWindow::windowStepUserMovedResized,
            this,
            &CubeSlideEffect::slotWindowStepUserMovedResized);
    connect(w,
            &EffectWindow::windowFinishUserMovedResized,
            this,
            &CubeSlideEffect::slotWindowFinishUserMovedResized);

    if (!isActive()) {
        return;
    }
    if (!shouldAnimate(w)) {
        staticWindows[w] = EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_DESKTOP);
        w->setData(WindowForceBlurRole, QVariant(true));
        w->setData(WindowForceBackgroundContrastRole, QVariant(true));
    }
}

void CubeSlideEffect::slotWindowDeleted(EffectWindow* w)
{
    staticWindows.remove(w);
}

bool CubeSlideEffect::shouldAnimate(const EffectWindow* w) const
{
    if (w->isDock()) {
        return !dontSlidePanels;
    }

    if (w->isOnAllDesktops()) {
        if (w->isDesktop()) {
            return true;
        }
        if (w->isSpecialWindow()) {
            return false;
        }
        return !dontSlideStickyWindows;
    }

    return true;
}

void CubeSlideEffect::slotWindowStepUserMovedResized(EffectWindow* w)
{
    if (!useWindowMoving) {
        return;
    }
    if (!effects->kwinOption(SwitchDesktopOnScreenEdgeMovingWindows).toBool()) {
        return;
    }
    if (w->isUserResize()) {
        return;
    }
    auto const screenSize = effects->virtualScreenSize();
    auto const cursor = effects->cursorPos();
    auto const horizontal = screenSize.width() * 0.1;
    auto const vertical = screenSize.height() * 0.1;

    QRect const leftRect(0, screenSize.height() * 0.1, horizontal, screenSize.height() * 0.8);
    QRect const rightRect(screenSize.width() - horizontal,
                          screenSize.height() * 0.1,
                          horizontal,
                          screenSize.height() * 0.8);
    QRect const topRect(horizontal, 0, screenSize.width() * 0.8, vertical);
    QRect const bottomRect(
        horizontal, screenSize.height() - vertical, screenSize.width() - horizontal * 2, vertical);

    if (leftRect.contains(cursor)) {
        if (effects->desktopToLeft(effects->currentDesktop()) != effects->currentDesktop()) {
            windowMovingChanged(0.3 * static_cast<float>(horizontal - cursor.x())
                                    / static_cast<float>(horizontal),
                                Left);
        }
    } else if (rightRect.contains(cursor)) {
        if (effects->desktopToRight(effects->currentDesktop()) != effects->currentDesktop()) {
            windowMovingChanged(
                0.3 * static_cast<float>(cursor.x() - screenSize.width() + horizontal)
                    / static_cast<float>(horizontal),
                Right);
        }
    } else if (topRect.contains(cursor)) {
        if (effects->desktopAbove(effects->currentDesktop()) != effects->currentDesktop()) {
            windowMovingChanged(0.3 * static_cast<float>(vertical - cursor.y())
                                    / static_cast<float>(vertical),
                                Upwards);
        }
    } else if (bottomRect.contains(cursor)) {
        if (effects->desktopBelow(effects->currentDesktop()) != effects->currentDesktop()) {
            windowMovingChanged(
                0.3 * static_cast<float>(cursor.y() - screenSize.height() + vertical)
                    / static_cast<float>(vertical),
                Downwards);
        }
    } else {
        // not in one of the areas
        windowMoving = false;
        desktopChangedWhileMoving = false;
        timeLine.setCurrentTime(0);
        lastPresentTime = std::chrono::milliseconds::zero();
        if (!slideRotations.isEmpty()) {
            slideRotations.clear();
        }
        effects->setActiveFullScreenEffect(nullptr);
        effects->addRepaintFull();
    }
}

void CubeSlideEffect::slotWindowFinishUserMovedResized(EffectWindow* w)
{
    if (!useWindowMoving) {
        return;
    }
    if (!effects->kwinOption(SwitchDesktopOnScreenEdgeMovingWindows).toBool()) {
        return;
    }
    if (w->isUserResize()) {
        return;
    }

    if (!desktopChangedWhileMoving) {
        if (slideRotations.isEmpty())
            return;
        const RotationDirection direction = slideRotations.dequeue();
        switch (direction) {
        case Left:
            slideRotations.enqueue(Right);
            break;
        case Right:
            slideRotations.enqueue(Left);
            break;
        case Upwards:
            slideRotations.enqueue(Downwards);
            break;
        case Downwards:
            slideRotations.enqueue(Upwards);
            break;
        default:
            break; // impossible
        }
        timeLine.setCurrentTime(timeLine.duration() - timeLine.currentTime());
    }

    desktopChangedWhileMoving = false;
    windowMoving = false;
    effects->addRepaintFull();
}

void CubeSlideEffect::windowMovingChanged(float progress, RotationDirection direction)
{
    if (desktopChangedWhileMoving) {
        progressRestriction = 1.0 - progress;
    } else {
        progressRestriction = progress;
    }

    front_desktop = effects->currentDesktop();

    if (slideRotations.isEmpty()) {
        slideRotations.enqueue(direction);
        windowMoving = true;
        startAnimation();
    }

    effects->addRepaintFull();
}

bool CubeSlideEffect::isActive() const
{
    return !slideRotations.isEmpty();
}

void CubeSlideEffect::slotNumberDesktopsChanged()
{
    // This effect animates only aftermaths of desktop switching. There is no any
    // way to reference removed desktops for animation purposes. So our the best
    // shot is just to do nothing. It doesn't look nice and we probaby have to
    // find more proper way to handle this case.

    if (!isActive()) {
        return;
    }

    auto const keys = staticWindows.keys();
    for (auto w : qAsConst(keys)) {
        w->setData(WindowForceBlurRole, QVariant());
        w->setData(WindowForceBackgroundContrastRole, QVariant());
    }

    slideRotations.clear();
    staticWindows.clear();
    lastPresentTime = std::chrono::milliseconds::zero();

    effects->setActiveFullScreenEffect(nullptr);
}

} // namespace
