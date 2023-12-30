/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "motions.h"

#include <render/effect/interface/effects_handler.h>

namespace KWin
{

/***************************************************************
 Motion1D
***************************************************************/

Motion1D::Motion1D(double initial, double strength, double smoothness)
    : Motion<double>(initial, strength, smoothness)
{
}

Motion1D::Motion1D(const Motion1D& other)
    : Motion<double>(other)
{
}

Motion1D::~Motion1D()
{
}

/***************************************************************
 Motion2D
***************************************************************/

Motion2D::Motion2D(QPointF initial, double strength, double smoothness)
    : Motion<QPointF>(initial, strength, smoothness)
{
}

Motion2D::Motion2D(const Motion2D& other)
    : Motion<QPointF>(other)
{
}

Motion2D::~Motion2D()
{
}

/***************************************************************
 WindowMotionManager
***************************************************************/

WindowMotionManager::WindowMotionManager(bool useGlobalAnimationModifier)
    : m_useGlobalAnimationModifier(useGlobalAnimationModifier)

{
    // TODO: Allow developer to modify motion attributes
} // TODO: What happens when the window moves by an external force?

WindowMotionManager::~WindowMotionManager()
{
}

void WindowMotionManager::manage(EffectWindow const* w)
{
    if (m_managedWindows.contains(w)) {
        return;
    }

    auto strength = 0.08;
    auto smoothness = 4.0;
    if (m_useGlobalAnimationModifier && effects->animationTimeFactor()) {
        // If the factor is == 0 then we just skip the calculation completely
        strength = 0.08 / effects->animationTimeFactor();
        smoothness = effects->animationTimeFactor() * 4.0;
    }

    auto& motion = m_managedWindows[w];
    motion.translation.setStrength(strength);
    motion.translation.setSmoothness(smoothness);
    motion.scale.setStrength(strength * 1.33);
    motion.scale.setSmoothness(smoothness / 2.0);

    motion.translation.setValue(w->pos());
    motion.scale.setValue(QPointF(1.0, 1.0));
}

void WindowMotionManager::unmanage(EffectWindow const* w)
{
    m_movingWindowsSet.remove(w);
    m_managedWindows.remove(w);
}

void WindowMotionManager::unmanageAll()
{
    m_managedWindows.clear();
    m_movingWindowsSet.clear();
}

void WindowMotionManager::calculate(int time)
{
    if (!effects->animationTimeFactor()) {
        // Just skip it completely if the user wants no animation
        m_movingWindowsSet.clear();
        auto it = m_managedWindows.begin();
        for (; it != m_managedWindows.end(); ++it) {
            auto motion = &it.value();
            motion->translation.finish();
            motion->scale.finish();
        }
    }

    auto it = m_managedWindows.begin();
    for (; it != m_managedWindows.end(); ++it) {
        auto motion = &it.value();
        int stopped = 0;

        // TODO: What happens when distance() == 0 but we are still moving fast?
        // TODO: Motion needs to be calculated from the window's center

        auto trans = &motion->translation;
        if (trans->distance().isNull()) {
            ++stopped;
        } else {
            // Still moving
            trans->calculate(time);
            const short fx = trans->target().x() <= trans->startValue().x() ? -1 : 1;
            const short fy = trans->target().y() <= trans->startValue().y() ? -1 : 1;
            if (trans->distance().x() * fx / 0.5 < 1.0 && trans->velocity().x() * fx / 0.2 < 1.0
                && trans->distance().y() * fy / 0.5 < 1.0
                && trans->velocity().y() * fy / 0.2 < 1.0) {
                // Hide tiny oscillations
                motion->translation.finish();
                ++stopped;
            }
        }

        Motion2D* scale = &motion->scale;
        if (scale->distance().isNull()) {
            ++stopped;
        } else {
            // Still scaling
            scale->calculate(time);
            const short fx = scale->target().x() < 1.0 ? -1 : 1;
            const short fy = scale->target().y() < 1.0 ? -1 : 1;
            if (scale->distance().x() * fx / 0.001 < 1.0 && scale->velocity().x() * fx / 0.05 < 1.0
                && scale->distance().y() * fy / 0.001 < 1.0
                && scale->velocity().y() * fy / 0.05 < 1.0) {
                // Hide tiny oscillations
                motion->scale.finish();
                ++stopped;
            }
        }

        // We just finished this window's motion
        if (stopped == 2)
            m_movingWindowsSet.remove(it.key());
    }
}

void WindowMotionManager::reset()
{
    auto it = m_managedWindows.begin();
    for (; it != m_managedWindows.end(); ++it) {
        auto motion = &it.value();
        auto window = it.key();
        motion->translation.setTarget(window->pos());
        motion->translation.finish();
        motion->scale.setTarget(QPointF(1.0, 1.0));
        motion->scale.finish();
    }
}

void WindowMotionManager::reset(EffectWindow const* w)
{
    auto it = m_managedWindows.find(w);
    if (it == m_managedWindows.end()) {
        return;
    }

    auto motion = &it.value();
    motion->translation.setTarget(w->pos());
    motion->translation.finish();
    motion->scale.setTarget(QPointF(1.0, 1.0));
    motion->scale.finish();
}

void WindowMotionManager::apply(effect::window_paint_data& data)
{
    auto it = m_managedWindows.find(&data.window);
    if (it == m_managedWindows.end()) {
        return;
    }

    // TODO: Take into account existing scale so that we can work with multiple managers (E.g.
    // Present windows + grid)
    auto motion = &it.value();
    data.paint.geo.translation
        += QVector3D(motion->translation.value() - QPointF(data.window.x(), data.window.y()));
    auto const scale = motion->scale.value();
    data.paint.geo.scale *= QVector3D(scale.x(), scale.y(), 1);
}

void WindowMotionManager::moveWindow(EffectWindow const* w,
                                     QPoint target,
                                     double scale,
                                     double yScale)
{
    auto it = m_managedWindows.find(w);
    if (it == m_managedWindows.end()) {
        // Notify the effect author that they did something wrong
        abort();
    }

    auto motion = &it.value();

    if (yScale == 0.0) {
        yScale = scale;
    }

    QPointF scalePoint(scale, yScale);

    if (motion->translation.value() == target && motion->scale.value() == scalePoint) {
        // Window already at that position
        return;
    }

    motion->translation.setTarget(target);
    motion->scale.setTarget(scalePoint);

    m_movingWindowsSet << w;
}

QRectF WindowMotionManager::transformedGeometry(EffectWindow const* w) const
{
    auto it = m_managedWindows.constFind(w);
    if (it == m_managedWindows.end()) {
        return w->frameGeometry();
    }

    auto const motion = &it.value();
    QRectF geometry(w->frameGeometry());

    // TODO: Take into account existing scale so that we can work with multiple managers (E.g.
    // Present windows + grid)
    geometry.moveTo(motion->translation.value());
    geometry.setWidth(geometry.width() * motion->scale.value().x());
    geometry.setHeight(geometry.height() * motion->scale.value().y());

    return geometry;
}

void WindowMotionManager::setTransformedGeometry(EffectWindow const* w, const QRectF& geometry)
{
    auto it = m_managedWindows.find(w);
    if (it == m_managedWindows.end()) {
        return;
    }

    auto motion = &it.value();
    motion->translation.setValue(geometry.topLeft());
    motion->scale.setValue(
        QPointF(geometry.width() / qreal(w->width()), geometry.height() / qreal(w->height())));
}

QRectF WindowMotionManager::targetGeometry(EffectWindow const* w) const
{
    auto it = m_managedWindows.constFind(w);
    if (it == m_managedWindows.end()) {
        return w->frameGeometry();
    }

    auto const motion = &it.value();
    QRectF geometry(w->frameGeometry());

    // TODO: Take into account existing scale so that we can work with multiple managers (E.g.
    // Present windows + grid)
    geometry.moveTo(motion->translation.target());
    geometry.setWidth(geometry.width() * motion->scale.target().x());
    geometry.setHeight(geometry.height() * motion->scale.target().y());

    return geometry;
}

EffectWindow const* WindowMotionManager::windowAtPoint(QPoint point, bool useStackingOrder) const
{
    Q_UNUSED(useStackingOrder);
    // TODO: Stacking order uses EffectsHandler::stackingOrder() then filters by m_managedWindows
    auto it = m_managedWindows.constBegin();
    while (it != m_managedWindows.constEnd()) {
        if (transformedGeometry(it.key()).contains(point))
            return it.key();
        ++it;
    }

    return nullptr;
}

}
