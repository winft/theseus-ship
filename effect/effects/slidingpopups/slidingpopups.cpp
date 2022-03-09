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
#include "slidingpopups.h"
#include "slidingpopupsconfig.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>

#include <KWindowEffects>
#include <QApplication>
#include <QFontMetrics>

namespace KWin
{

void sanitize_anim_data(effect::anim_update& data,
                        std::chrono::milliseconds const& in_fallback,
                        std::chrono::milliseconds const& out_fallback)
{
    auto const screen_area = effects->clientArea(
        FullScreenArea, data.base.window->screen(), effects->currentDesktop());
    auto const win_geo = data.base.window->frameGeometry();

    if (data.offset == -1) {
        switch (data.location) {
        case effect::position::left:
            data.offset = qMax(win_geo.left() - screen_area.left(), 0);
            break;
        case effect::position::top:
            data.offset = qMax(win_geo.top() - screen_area.top(), 0);
            break;
        case effect::position::right:
            data.offset = qMax(screen_area.right() - win_geo.right(), 0);
            break;
        case effect::position::bottom:
        default:
            data.offset = qMax(screen_area.bottom() - win_geo.bottom(), 0);
            break;
        }
    }

    switch (data.location) {
    case effect::position::left:
        data.offset = std::max<double>(win_geo.left() - screen_area.left(), data.offset);
        break;
    case effect::position::top:
        data.offset = std::max<double>(win_geo.top() - screen_area.top(), data.offset);
        break;
    case effect::position::right:
        data.offset = std::max<double>(screen_area.right() - win_geo.right(), data.offset);
        break;
    case effect::position::bottom:
    default:
        data.offset = std::max<double>(screen_area.bottom() - win_geo.bottom(), data.offset);
        break;
    }

    if (!data.in.count()) {
        data.in = in_fallback;
    }
    if (!data.out.count()) {
        data.out = out_fallback;
    }
}

void update_function(SlidingPopupsEffect& effect, KWin::effect::anim_update const& update)
{
    // Should always come with a window.
    auto window = update.base.window;
    assert(window);

    if (!update.base.valid) {
        // Property was removed, thus also remove the effect for window.
        if (window->data(WindowClosedGrabRole).value<void*>() == &effect) {
            window->setData(WindowClosedGrabRole, QVariant());
        }
        effect.m_animations.remove(window);
        effect.m_animationsData.remove(window);
        return;
    }

    auto const window_added = !effect.m_animationsData.contains(window);
    auto& data = effect.m_animationsData[window];
    data = update;
    sanitize_anim_data(data, effect.m_slideInDuration, effect.m_slideOutDuration);

    // Grab the window, so other windowClosed effects will ignore it
    data.base.window->setData(WindowClosedGrabRole,
                              QVariant::fromValue(static_cast<void*>(&effect)));

    if (window_added) {
        effect.slideIn(window);
    }
}

SlidingPopupsEffect::SlidingPopupsEffect()
{
    initConfig<SlidingPopupsConfig>();
    m_slideLength = QFontMetrics(qApp->font()).height() * 8;

    connect(effects, &EffectsHandler::windowClosed, this, &SlidingPopupsEffect::slideOut);
    connect(effects, &EffectsHandler::windowDeleted, this, &SlidingPopupsEffect::slotWindowDeleted);
    connect(effects, &EffectsHandler::windowShown, this, &SlidingPopupsEffect::slideIn);
    connect(effects, &EffectsHandler::windowHidden, this, &SlidingPopupsEffect::slideOut);
    connect(effects,
            qOverload<int, int, EffectWindow*>(&EffectsHandler::desktopChanged),
            this,
            &SlidingPopupsEffect::stopAnimations);
    connect(effects,
            &EffectsHandler::activeFullScreenEffectChanged,
            this,
            &SlidingPopupsEffect::stopAnimations);

    reconfigure(ReconfigureAll);

    auto& slide_integration = effects->get_slide_integration();
    auto update = [this](auto&& data) { update_function(*this, data); };
    slide_integration.add(*this, update);
}

SlidingPopupsEffect::~SlidingPopupsEffect()
{
}

bool SlidingPopupsEffect::supported()
{
    return effects->animationsSupported();
}

void SlidingPopupsEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    SlidingPopupsConfig::self()->read();
    m_slideInDuration = std::chrono::milliseconds(static_cast<int>(animationTime(
        SlidingPopupsConfig::slideInTime() != 0 ? SlidingPopupsConfig::slideInTime() : 150)));
    m_slideOutDuration = std::chrono::milliseconds(static_cast<int>(animationTime(
        SlidingPopupsConfig::slideOutTime() != 0 ? SlidingPopupsConfig::slideOutTime() : 250)));

    auto animationIt = m_animations.begin();
    while (animationIt != m_animations.end()) {
        const auto duration
            = ((*animationIt).kind == AnimationKind::In) ? m_slideInDuration : m_slideOutDuration;
        (*animationIt).timeLine.setDuration(duration);
        ++animationIt;
    }

    auto dataIt = m_animationsData.begin();
    while (dataIt != m_animationsData.end()) {
        (*dataIt).in = m_slideInDuration;
        (*dataIt).out = m_slideOutDuration;
        ++dataIt;
    }
}

void SlidingPopupsEffect::prePaintWindow(EffectWindow* w,
                                         WindowPrePaintData& data,
                                         std::chrono::milliseconds presentTime)
{
    auto animationIt = m_animations.find(w);
    if (animationIt == m_animations.end()) {
        effects->prePaintWindow(w, data, presentTime);
        return;
    }

    std::chrono::milliseconds delta = std::chrono::milliseconds::zero();
    if (animationIt->lastPresentTime.count()) {
        delta = presentTime - animationIt->lastPresentTime;
    }
    animationIt->lastPresentTime = presentTime;

    (*animationIt).timeLine.update(delta);
    data.setTransformed();
    w->enablePainting(EffectWindow::PAINT_DISABLED | EffectWindow::PAINT_DISABLED_BY_DELETE);

    effects->prePaintWindow(w, data, presentTime);
}

void SlidingPopupsEffect::paintWindow(EffectWindow* w,
                                      int mask,
                                      QRegion region,
                                      WindowPaintData& data)
{
    auto animationIt = m_animations.constFind(w);
    if (animationIt == m_animations.constEnd()) {
        effects->paintWindow(w, mask, region, data);
        return;
    }

    auto const& animData = m_animationsData[w];
    const int slideLength = (animData.distance > 0) ? animData.distance : m_slideLength;

    const QRect screenRect
        = effects->clientArea(FullScreenArea, w->screen(), effects->currentDesktop());
    int splitPoint = 0;
    const QRect geo = w->expandedGeometry();
    const qreal t = (*animationIt).timeLine.value();

    switch (animData.location) {
    case effect::position::left:
        if (slideLength < geo.width()) {
            data.multiplyOpacity(t);
        }
        data.translate(-interpolate(qMin(geo.width(), slideLength), 0.0, t));
        splitPoint = geo.width() - (geo.x() + geo.width() - screenRect.x() - animData.offset);
        region &= QRegion(geo.x() + splitPoint, geo.y(), geo.width() - splitPoint, geo.height());
        break;
    case effect::position::top:
        if (slideLength < geo.height()) {
            data.multiplyOpacity(t);
        }
        data.translate(0.0, -interpolate(qMin(geo.height(), slideLength), 0.0, t));
        splitPoint = geo.height() - (geo.y() + geo.height() - screenRect.y() - animData.offset);
        region &= QRegion(geo.x(), geo.y() + splitPoint, geo.width(), geo.height() - splitPoint);
        break;
    case effect::position::right:
        if (slideLength < geo.width()) {
            data.multiplyOpacity(t);
        }
        data.translate(interpolate(qMin(geo.width(), slideLength), 0.0, t));
        splitPoint = screenRect.x() + screenRect.width() - geo.x() - animData.offset;
        region &= QRegion(geo.x(), geo.y(), splitPoint, geo.height());
        break;
    case effect::position::bottom:
    default:
        if (slideLength < geo.height()) {
            data.multiplyOpacity(t);
        }
        data.translate(0.0, interpolate(qMin(geo.height(), slideLength), 0.0, t));
        splitPoint = screenRect.y() + screenRect.height() - geo.y() - animData.offset;
        region &= QRegion(geo.x(), geo.y(), geo.width(), splitPoint);
    }

    effects->paintWindow(w, mask, region, data);
}

void SlidingPopupsEffect::postPaintWindow(EffectWindow* w)
{
    auto animationIt = m_animations.find(w);
    if (animationIt != m_animations.end()) {
        if ((*animationIt).timeLine.done()) {
            if (w->isDeleted()) {
                w->unrefWindow();
            } else {
                w->setData(WindowForceBackgroundContrastRole, QVariant());
                w->setData(WindowForceBlurRole, QVariant());
            }
            m_animations.erase(animationIt);
        }
        effects->addRepaint(w->expandedGeometry());
    }

    effects->postPaintWindow(w);
}

void SlidingPopupsEffect::slotWindowDeleted(EffectWindow* w)
{
    m_animations.remove(w);
    m_animationsData.remove(w);
}

void SlidingPopupsEffect::slideIn(EffectWindow* w)
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!w->isVisible()) {
        return;
    }

    auto dataIt = m_animationsData.constFind(w);
    if (dataIt == m_animationsData.constEnd()) {
        return;
    }

    Animation& animation = m_animations[w];
    animation.kind = AnimationKind::In;
    animation.timeLine.setDirection(TimeLine::Forward);
    animation.timeLine.setDuration((*dataIt).in);
    animation.timeLine.setEasingCurve(QEasingCurve::OutCubic);

    // If the opposite animation (Out) was active and it had shorter duration,
    // at this point, the timeline can end up in the "done" state. Thus, we have
    // to reset it.
    if (animation.timeLine.done()) {
        animation.timeLine.reset();
    }

    w->setData(WindowAddedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    w->setData(WindowForceBackgroundContrastRole, QVariant(true));
    w->setData(WindowForceBlurRole, QVariant(true));

    w->addRepaintFull();
}

void SlidingPopupsEffect::slideOut(EffectWindow* w)
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!w->isVisible()) {
        return;
    }

    auto dataIt = m_animationsData.constFind(w);
    if (dataIt == m_animationsData.constEnd()) {
        return;
    }

    if (w->isDeleted()) {
        w->refWindow();
    }

    Animation& animation = m_animations[w];
    animation.kind = AnimationKind::Out;
    animation.timeLine.setDirection(TimeLine::Backward);
    animation.timeLine.setDuration((*dataIt).out);
    // this is effectively InCubic because the direction is reversed
    animation.timeLine.setEasingCurve(QEasingCurve::OutCubic);

    // If the opposite animation (In) was active and it had shorter duration,
    // at this point, the timeline can end up in the "done" state. Thus, we have
    // to reset it.
    if (animation.timeLine.done()) {
        animation.timeLine.reset();
    }

    w->setData(WindowClosedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    w->setData(WindowForceBackgroundContrastRole, QVariant(true));
    w->setData(WindowForceBlurRole, QVariant(true));

    w->addRepaintFull();
}

void SlidingPopupsEffect::stopAnimations()
{
    for (auto it = m_animations.constBegin(); it != m_animations.constEnd(); ++it) {
        EffectWindow* w = it.key();

        if (w->isDeleted()) {
            w->unrefWindow();
        } else {
            w->setData(WindowForceBackgroundContrastRole, QVariant());
            w->setData(WindowForceBlurRole, QVariant());
        }
    }

    m_animations.clear();
}

bool SlidingPopupsEffect::isActive() const
{
    return !m_animations.isEmpty();
}

} // namespace
