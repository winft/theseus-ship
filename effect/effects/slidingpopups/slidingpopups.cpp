/*
    SPDX-FileCopyrightText: 2009 Marco Martin <notmart@gmail.com>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "slidingpopups.h"
#include "slidingpopupsconfig.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>

#include <QFontMetrics>
#include <QGuiApplication>

namespace KWin
{

void sanitize_anim_data(effect::anim_update& data,
                        std::chrono::milliseconds const& in_fallback,
                        std::chrono::milliseconds const& out_fallback)
{
    auto const screen_area = effects->clientArea(
        FullScreenArea, data.base.window->screen(), effects->currentDesktop());
    auto const win_geo = data.base.window->frameGeometry();

    // Per convention offset -1 indicates that the effect should choose.
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
        effect.animations.remove(window);
        effect.window_data.remove(window);
        return;
    }

    auto const window_added = !effect.window_data.contains(window);
    auto& data = effect.window_data[window];
    data = update;
    sanitize_anim_data(data, effect.config.in, effect.config.out);

    // Grab the window, so other windowClosed effects will ignore it
    data.base.window->setData(WindowClosedGrabRole,
                              QVariant::fromValue(static_cast<void*>(&effect)));

    if (window_added) {
        effect.slide_in(window);
    }
}

SlidingPopupsEffect::SlidingPopupsEffect()
{
    initConfig<SlidingPopupsConfig>();
    config.distance = QFontMetrics(QGuiApplication::font()).height() * 8;

    connect(effects, &EffectsHandler::windowClosed, this, &SlidingPopupsEffect::slide_out);
    connect(
        effects, &EffectsHandler::windowDeleted, this, &SlidingPopupsEffect::handle_window_deleted);
    connect(effects, &EffectsHandler::windowShown, this, &SlidingPopupsEffect::slide_in);
    connect(effects, &EffectsHandler::windowHidden, this, &SlidingPopupsEffect::slide_out);
    connect(effects, &EffectsHandler::desktopChanged, this, &SlidingPopupsEffect::stopAnimations);
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
    config.in = std::chrono::milliseconds(static_cast<int>(animationTime(
        SlidingPopupsConfig::slideInTime() != 0 ? SlidingPopupsConfig::slideInTime() : 150)));
    config.out = std::chrono::milliseconds(static_cast<int>(animationTime(
        SlidingPopupsConfig::slideOutTime() != 0 ? SlidingPopupsConfig::slideOutTime() : 250)));

    auto animationIt = animations.begin();
    while (animationIt != animations.end()) {
        auto const duration = ((*animationIt).kind == AnimationKind::In) ? config.in : config.out;
        (*animationIt).timeline.setDuration(duration);
        ++animationIt;
    }

    auto dataIt = window_data.begin();
    while (dataIt != window_data.end()) {
        (*dataIt).in = config.in;
        (*dataIt).out = config.out;
        ++dataIt;
    }
}

void SlidingPopupsEffect::prePaintWindow(EffectWindow* win,
                                         WindowPrePaintData& data,
                                         std::chrono::milliseconds presentTime)
{
    auto animationIt = animations.find(win);
    if (animationIt == animations.end()) {
        effects->prePaintWindow(win, data, presentTime);
        return;
    }

    (*animationIt).timeline.advance(presentTime);
    data.setTransformed();
    win->enablePainting(EffectWindow::PAINT_DISABLED | EffectWindow::PAINT_DISABLED_BY_DELETE);

    effects->prePaintWindow(win, data, presentTime);
}

void SlidingPopupsEffect::paintWindow(EffectWindow* win,
                                      int mask,
                                      QRegion region,
                                      WindowPaintData& data)
{
    auto animationIt = animations.constFind(win);
    if (animationIt == animations.constEnd()) {
        effects->paintWindow(win, mask, region, data);
        return;
    }

    auto const& animData = window_data[win];
    int const slideLength = (animData.distance > 0) ? animData.distance : config.distance;

    int split_point = 0;
    auto const screen_area
        = effects->clientArea(FullScreenArea, win->screen(), effects->currentDesktop());
    auto const geo = win->expandedGeometry();
    auto const time = (*animationIt).timeline.value();

    switch (animData.location) {
    case effect::position::left:
        if (slideLength < geo.width()) {
            data.multiplyOpacity(time);
        }
        data.translate(-interpolate(qMin(geo.width(), slideLength), 0.0, time));
        split_point = geo.width() - (geo.x() + geo.width() - screen_area.x() - animData.offset);
        region &= QRegion(geo.x() + split_point, geo.y(), geo.width() - split_point, geo.height());
        break;
    case effect::position::top:
        if (slideLength < geo.height()) {
            data.multiplyOpacity(time);
        }
        data.translate(0.0, -interpolate(qMin(geo.height(), slideLength), 0.0, time));
        split_point = geo.height() - (geo.y() + geo.height() - screen_area.y() - animData.offset);
        region &= QRegion(geo.x(), geo.y() + split_point, geo.width(), geo.height() - split_point);
        break;
    case effect::position::right:
        if (slideLength < geo.width()) {
            data.multiplyOpacity(time);
        }
        data.translate(interpolate(qMin(geo.width(), slideLength), 0.0, time));
        split_point = screen_area.x() + screen_area.width() - geo.x() - animData.offset;
        region &= QRegion(geo.x(), geo.y(), split_point, geo.height());
        break;
    case effect::position::bottom:
    default:
        if (slideLength < geo.height()) {
            data.multiplyOpacity(time);
        }
        data.translate(0.0, interpolate(qMin(geo.height(), slideLength), 0.0, time));
        split_point = screen_area.y() + screen_area.height() - geo.y() - animData.offset;
        region &= QRegion(geo.x(), geo.y(), geo.width(), split_point);
    }

    effects->paintWindow(win, mask, region, data);
}

void SlidingPopupsEffect::postPaintWindow(EffectWindow* win)
{
    auto animationIt = animations.find(win);
    if (animationIt != animations.end()) {
        if ((*animationIt).timeline.done()) {
            if (win->isDeleted()) {
                win->unrefWindow();
            } else {
                win->setData(WindowForceBackgroundContrastRole, QVariant());
                win->setData(WindowForceBlurRole, QVariant());
            }
            animations.erase(animationIt);
        }
        effects->addRepaint(win->expandedGeometry());
    }

    effects->postPaintWindow(win);
}

void SlidingPopupsEffect::handle_window_deleted(EffectWindow* win)
{
    animations.remove(win);
    window_data.remove(win);
}

void SlidingPopupsEffect::slide_in(EffectWindow* win)
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!win->isVisible()) {
        return;
    }

    auto dataIt = window_data.constFind(win);
    if (dataIt == window_data.constEnd()) {
        return;
    }

    auto& animation = animations[win];
    animation.kind = AnimationKind::In;
    animation.timeline.setDirection(TimeLine::Forward);
    animation.timeline.setDuration((*dataIt).in);
    animation.timeline.setEasingCurve(QEasingCurve::OutCubic);

    // If the opposite animation (Out) was active and it had shorter duration,
    // at this point, the timeline can end up in the "done" state. Thus, we have
    // to reset it.
    if (animation.timeline.done()) {
        animation.timeline.reset();
    }

    win->setData(WindowAddedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    win->setData(WindowForceBackgroundContrastRole, QVariant(true));
    win->setData(WindowForceBlurRole, QVariant(true));

    win->addRepaintFull();
}

void SlidingPopupsEffect::slide_out(EffectWindow* win)
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!win->isVisible()) {
        return;
    }

    auto dataIt = window_data.constFind(win);
    if (dataIt == window_data.constEnd()) {
        return;
    }

    if (win->isDeleted()) {
        win->refWindow();
    }

    auto& animation = animations[win];
    animation.kind = AnimationKind::Out;
    animation.timeline.setDirection(TimeLine::Backward);
    animation.timeline.setDuration((*dataIt).out);

    // this is effectively InCubic because the direction is reversed
    animation.timeline.setEasingCurve(QEasingCurve::OutCubic);

    // If the opposite animation (In) was active and it had shorter duration,
    // at this point, the timeline can end up in the "done" state. Thus, we have
    // to reset it.
    if (animation.timeline.done()) {
        animation.timeline.reset();
    }

    win->setData(WindowClosedGrabRole, QVariant::fromValue(static_cast<void*>(this)));
    win->setData(WindowForceBackgroundContrastRole, QVariant(true));
    win->setData(WindowForceBlurRole, QVariant(true));

    win->addRepaintFull();
}

void SlidingPopupsEffect::stopAnimations()
{
    for (auto it = animations.constBegin(); it != animations.constEnd(); ++it) {
        auto win = it.key();

        if (win->isDeleted()) {
            win->unrefWindow();
        } else {
            win->setData(WindowForceBackgroundContrastRole, QVariant());
            win->setData(WindowForceBlurRole, QVariant());
        }
    }

    animations.clear();
}

bool SlidingPopupsEffect::isActive() const
{
    return !animations.isEmpty();
}

}
