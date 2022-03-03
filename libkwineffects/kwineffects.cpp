/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>
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

#include "kwineffects.h"

#include "config-kwin.h"
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
#include "kwinxrenderutils.h"
#endif

#include <QFontMetrics>
#include <QList>
#include <QMatrix4x4>
#include <QPainter>
#include <QPixmap>
#include <QTimeLine>
#include <QVariant>
#include <QVector2D>
#include <QtMath>

#ifdef KWIN_HAVE_XRENDER_COMPOSITING
#include <xcb/xfixes.h>
#endif

namespace KWin
{

/***************************************************************
 PaintClipper
***************************************************************/

QStack<QRegion>* PaintClipper::areas = nullptr;

PaintClipper::PaintClipper(const QRegion& allowed_area)
    : area(allowed_area)
{
    push(area);
}

PaintClipper::~PaintClipper()
{
    pop(area);
}

void PaintClipper::push(const QRegion& allowed_area)
{
    if (allowed_area == infiniteRegion()) // don't push these
        return;
    if (areas == nullptr)
        areas = new QStack<QRegion>;
    areas->push(allowed_area);
}

void PaintClipper::pop(const QRegion& allowed_area)
{
    if (allowed_area == infiniteRegion())
        return;
    Q_ASSERT(areas != nullptr);
    Q_ASSERT(areas->top() == allowed_area);
    areas->pop();
    if (areas->isEmpty()) {
        delete areas;
        areas = nullptr;
    }
}

bool PaintClipper::clip()
{
    return areas != nullptr;
}

QRegion PaintClipper::paintArea()
{
    Q_ASSERT(areas != nullptr); // can be called only with clip() == true
    const QSize& s = effects->virtualScreenSize();
    QRegion ret(0, 0, s.width(), s.height());
    for (const QRegion& r : qAsConst(*areas)) {
        ret &= r;
    }
    return ret;
}

struct PaintClipper::Iterator::Data {
    Data()
        : index(0)
    {
    }
    int index;
    QRegion region;
};

PaintClipper::Iterator::Iterator()
    : data(new Data)
{
    if (clip() && effects->isOpenGLCompositing()) {
        data->region = paintArea();
        data->index = -1;
        next(); // move to the first one
    }
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    if (clip() && effects->compositingType() == XRenderCompositing) {
        XFixesRegion region(paintArea());
        xcb_xfixes_set_picture_clip_region(
            connection(), effects->xrenderBufferPicture(), region, 0, 0);
    }
#endif
}

PaintClipper::Iterator::~Iterator()
{
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    if (clip() && effects->compositingType() == XRenderCompositing)
        xcb_xfixes_set_picture_clip_region(
            connection(), effects->xrenderBufferPicture(), XCB_XFIXES_REGION_NONE, 0, 0);
#endif
    delete data;
}

bool PaintClipper::Iterator::isDone()
{
    if (!clip())
        return data->index == 1; // run once
    if (effects->isOpenGLCompositing())
        return data->index >= data->region.rectCount(); // run once per each area
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    if (effects->compositingType() == XRenderCompositing)
        return data->index == 1; // run once
#endif
    abort();
}

void PaintClipper::Iterator::next()
{
    data->index++;
}

QRect PaintClipper::Iterator::boundingRect() const
{
    if (!clip())
        return infiniteRegion();
    if (effects->isOpenGLCompositing())
        return *(data->region.begin() + data->index);
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    if (effects->compositingType() == XRenderCompositing)
        return data->region.boundingRect();
#endif
    abort();
    return infiniteRegion();
}

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

void WindowMotionManager::manage(EffectWindow* w)
{
    if (m_managedWindows.contains(w))
        return;

    double strength = 0.08;
    double smoothness = 4.0;
    if (m_useGlobalAnimationModifier && effects->animationTimeFactor()) {
        // If the factor is == 0 then we just skip the calculation completely
        strength = 0.08 / effects->animationTimeFactor();
        smoothness = effects->animationTimeFactor() * 4.0;
    }

    WindowMotion& motion = m_managedWindows[w];
    motion.translation.setStrength(strength);
    motion.translation.setSmoothness(smoothness);
    motion.scale.setStrength(strength * 1.33);
    motion.scale.setSmoothness(smoothness / 2.0);

    motion.translation.setValue(w->pos());
    motion.scale.setValue(QPointF(1.0, 1.0));
}

void WindowMotionManager::unmanage(EffectWindow* w)
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
        QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.begin();
        for (; it != m_managedWindows.end(); ++it) {
            WindowMotion* motion = &it.value();
            motion->translation.finish();
            motion->scale.finish();
        }
    }

    QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.begin();
    for (; it != m_managedWindows.end(); ++it) {
        WindowMotion* motion = &it.value();
        int stopped = 0;

        // TODO: What happens when distance() == 0 but we are still moving fast?
        // TODO: Motion needs to be calculated from the window's center

        Motion2D* trans = &motion->translation;
        if (trans->distance().isNull())
            ++stopped;
        else {
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
        if (scale->distance().isNull())
            ++stopped;
        else {
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
    QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.begin();
    for (; it != m_managedWindows.end(); ++it) {
        WindowMotion* motion = &it.value();
        EffectWindow* window = it.key();
        motion->translation.setTarget(window->pos());
        motion->translation.finish();
        motion->scale.setTarget(QPointF(1.0, 1.0));
        motion->scale.finish();
    }
}

void WindowMotionManager::reset(EffectWindow* w)
{
    QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.find(w);
    if (it == m_managedWindows.end())
        return;

    WindowMotion* motion = &it.value();
    motion->translation.setTarget(w->pos());
    motion->translation.finish();
    motion->scale.setTarget(QPointF(1.0, 1.0));
    motion->scale.finish();
}

void WindowMotionManager::apply(EffectWindow* w, WindowPaintData& data)
{
    QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.find(w);
    if (it == m_managedWindows.end())
        return;

    // TODO: Take into account existing scale so that we can work with multiple managers (E.g.
    // Present windows + grid)
    WindowMotion* motion = &it.value();
    data += (motion->translation.value() - QPointF(w->x(), w->y()));
    data *= QVector2D(motion->scale.value());
}

void WindowMotionManager::moveWindow(EffectWindow* w, QPoint target, double scale, double yScale)
{
    QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.find(w);
    if (it == m_managedWindows.end())
        abort(); // Notify the effect author that they did something wrong

    WindowMotion* motion = &it.value();

    if (yScale == 0.0)
        yScale = scale;
    QPointF scalePoint(scale, yScale);

    if (motion->translation.value() == target && motion->scale.value() == scalePoint)
        return; // Window already at that position

    motion->translation.setTarget(target);
    motion->scale.setTarget(scalePoint);

    m_movingWindowsSet << w;
}

QRectF WindowMotionManager::transformedGeometry(EffectWindow* w) const
{
    QHash<EffectWindow*, WindowMotion>::const_iterator it = m_managedWindows.constFind(w);
    if (it == m_managedWindows.end())
        return w->frameGeometry();

    const WindowMotion* motion = &it.value();
    QRectF geometry(w->frameGeometry());

    // TODO: Take into account existing scale so that we can work with multiple managers (E.g.
    // Present windows + grid)
    geometry.moveTo(motion->translation.value());
    geometry.setWidth(geometry.width() * motion->scale.value().x());
    geometry.setHeight(geometry.height() * motion->scale.value().y());

    return geometry;
}

void WindowMotionManager::setTransformedGeometry(EffectWindow* w, const QRectF& geometry)
{
    QHash<EffectWindow*, WindowMotion>::iterator it = m_managedWindows.find(w);
    if (it == m_managedWindows.end())
        return;
    WindowMotion* motion = &it.value();
    motion->translation.setValue(geometry.topLeft());
    motion->scale.setValue(
        QPointF(geometry.width() / qreal(w->width()), geometry.height() / qreal(w->height())));
}

QRectF WindowMotionManager::targetGeometry(EffectWindow* w) const
{
    QHash<EffectWindow*, WindowMotion>::const_iterator it = m_managedWindows.constFind(w);
    if (it == m_managedWindows.end())
        return w->frameGeometry();

    const WindowMotion* motion = &it.value();
    QRectF geometry(w->frameGeometry());

    // TODO: Take into account existing scale so that we can work with multiple managers (E.g.
    // Present windows + grid)
    geometry.moveTo(motion->translation.target());
    geometry.setWidth(geometry.width() * motion->scale.target().x());
    geometry.setHeight(geometry.height() * motion->scale.target().y());

    return geometry;
}

EffectWindow* WindowMotionManager::windowAtPoint(QPoint point, bool useStackingOrder) const
{
    Q_UNUSED(useStackingOrder);
    // TODO: Stacking order uses EffectsHandler::stackingOrder() then filters by m_managedWindows
    QHash<EffectWindow*, WindowMotion>::ConstIterator it = m_managedWindows.constBegin();
    while (it != m_managedWindows.constEnd()) {
        if (transformedGeometry(it.key()).contains(point))
            return it.key();
        ++it;
    }

    return nullptr;
}

/***************************************************************
 EffectFramePrivate
***************************************************************/
class EffectFramePrivate
{
public:
    EffectFramePrivate();
    ~EffectFramePrivate();

    bool crossFading;
    qreal crossFadeProgress;
    QMatrix4x4 screenProjectionMatrix;
};

EffectFramePrivate::EffectFramePrivate()
    : crossFading(false)
    , crossFadeProgress(1.0)
{
}

EffectFramePrivate::~EffectFramePrivate()
{
}

/***************************************************************
 EffectFrame
***************************************************************/
EffectFrame::EffectFrame()
    : d(new EffectFramePrivate)
{
}

EffectFrame::~EffectFrame()
{
    delete d;
}

qreal EffectFrame::crossFadeProgress() const
{
    return d->crossFadeProgress;
}

void EffectFrame::setCrossFadeProgress(qreal progress)
{
    d->crossFadeProgress = progress;
}

bool EffectFrame::isCrossFade() const
{
    return d->crossFading;
}

void EffectFrame::enableCrossFade(bool enable)
{
    d->crossFading = enable;
}

QMatrix4x4 EffectFrame::screenProjectionMatrix() const
{
    return d->screenProjectionMatrix;
}

void EffectFrame::setScreenProjectionMatrix(const QMatrix4x4& spm)
{
    d->screenProjectionMatrix = spm;
}

/***************************************************************
 TimeLine
***************************************************************/

class Q_DECL_HIDDEN TimeLine::Data : public QSharedData
{
public:
    std::chrono::milliseconds duration;
    Direction direction;
    QEasingCurve easingCurve;

    std::chrono::milliseconds elapsed = std::chrono::milliseconds::zero();
    bool done = false;
    RedirectMode sourceRedirectMode = RedirectMode::Relaxed;
    RedirectMode targetRedirectMode = RedirectMode::Strict;
};

TimeLine::TimeLine(std::chrono::milliseconds duration, Direction direction)
    : d(new Data)
{
    Q_ASSERT(duration > std::chrono::milliseconds::zero());
    d->duration = duration;
    d->direction = direction;
}

TimeLine::TimeLine(const TimeLine& other)
    : d(other.d)
{
}

TimeLine::~TimeLine() = default;

qreal TimeLine::progress() const
{
    return static_cast<qreal>(d->elapsed.count()) / d->duration.count();
}

qreal TimeLine::value() const
{
    const qreal t = progress();
    return d->easingCurve.valueForProgress(d->direction == Backward ? 1.0 - t : t);
}

void TimeLine::update(std::chrono::milliseconds delta)
{
    Q_ASSERT(delta >= std::chrono::milliseconds::zero());
    if (d->done) {
        return;
    }
    d->elapsed += delta;
    if (d->elapsed >= d->duration) {
        d->done = true;
        d->elapsed = d->duration;
    }
}

std::chrono::milliseconds TimeLine::elapsed() const
{
    return d->elapsed;
}

void TimeLine::setElapsed(std::chrono::milliseconds elapsed)
{
    Q_ASSERT(elapsed >= std::chrono::milliseconds::zero());
    if (elapsed == d->elapsed) {
        return;
    }
    reset();
    update(elapsed);
}

std::chrono::milliseconds TimeLine::duration() const
{
    return d->duration;
}

void TimeLine::setDuration(std::chrono::milliseconds duration)
{
    Q_ASSERT(duration > std::chrono::milliseconds::zero());
    if (duration == d->duration) {
        return;
    }
    d->elapsed = std::chrono::milliseconds(qRound(progress() * duration.count()));
    d->duration = duration;
    if (d->elapsed == d->duration) {
        d->done = true;
    }
}

TimeLine::Direction TimeLine::direction() const
{
    return d->direction;
}

void TimeLine::setDirection(TimeLine::Direction direction)
{
    if (d->direction == direction) {
        return;
    }

    d->direction = direction;

    if (d->elapsed > std::chrono::milliseconds::zero()
        || d->sourceRedirectMode == RedirectMode::Strict) {
        d->elapsed = d->duration - d->elapsed;
    }

    if (d->done && d->targetRedirectMode == RedirectMode::Relaxed) {
        d->done = false;
    }

    if (d->elapsed >= d->duration) {
        d->done = true;
    }
}

void TimeLine::toggleDirection()
{
    setDirection(d->direction == Forward ? Backward : Forward);
}

QEasingCurve TimeLine::easingCurve() const
{
    return d->easingCurve;
}

void TimeLine::setEasingCurve(const QEasingCurve& easingCurve)
{
    d->easingCurve = easingCurve;
}

void TimeLine::setEasingCurve(QEasingCurve::Type type)
{
    d->easingCurve.setType(type);
}

bool TimeLine::running() const
{
    return d->elapsed != std::chrono::milliseconds::zero() && d->elapsed != d->duration;
}

bool TimeLine::done() const
{
    return d->done;
}

void TimeLine::reset()
{
    d->elapsed = std::chrono::milliseconds::zero();
    d->done = false;
}

TimeLine::RedirectMode TimeLine::sourceRedirectMode() const
{
    return d->sourceRedirectMode;
}

void TimeLine::setSourceRedirectMode(RedirectMode mode)
{
    d->sourceRedirectMode = mode;
}

TimeLine::RedirectMode TimeLine::targetRedirectMode() const
{
    return d->targetRedirectMode;
}

void TimeLine::setTargetRedirectMode(RedirectMode mode)
{
    d->targetRedirectMode = mode;
}

TimeLine& TimeLine::operator=(const TimeLine& other)
{
    d = other.d;
    return *this;
}

} // namespace

#include "moc_kwinglobals.cpp"
