/*
SPDX-FileCopyrightText: 2007 Philip Falkner <philip.falkner@gmail.com>
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2010 Alexandre Pereira <pereira.alex@gmail.com>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "glide.h"

// KConfigSkeleton
#include "glideconfig.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>
#include <kwingl/utils.h>

#include <QMatrix4x4>
#include <QSet>

namespace KWin
{

static const QSet<QString> s_blacklist{
    QStringLiteral("ksmserver ksmserver"),
    QStringLiteral("ksmserver-logout-greeter ksmserver-logout-greeter"),
    QStringLiteral("ksplashqml ksplashqml"),
    // Spectacle needs to be blacklisted in order to stay out of its own screenshots.
    QStringLiteral("spectacle spectacle"),         // x11
    QStringLiteral("spectacle org.kde.spectacle"), // wayland
};

GlideEffect::GlideEffect()
{
    initConfig<GlideConfig>();
    reconfigure(ReconfigureAll);

    connect(effects, &EffectsHandler::windowAdded, this, &GlideEffect::windowAdded);
    connect(effects, &EffectsHandler::windowClosed, this, &GlideEffect::windowClosed);
    connect(effects, &EffectsHandler::windowDeleted, this, &GlideEffect::windowDeleted);
    connect(effects, &EffectsHandler::windowDataChanged, this, &GlideEffect::windowDataChanged);
}

GlideEffect::~GlideEffect() = default;

void GlideEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    GlideConfig::self()->read();
    m_duration = std::chrono::milliseconds(animationTime<GlideConfig>(160));

    m_inParams.edge = static_cast<RotationEdge>(GlideConfig::inRotationEdge());
    m_inParams.angle.from = GlideConfig::inRotationAngle();
    m_inParams.angle.to = 0.0;
    m_inParams.distance.from = GlideConfig::inDistance();
    m_inParams.distance.to = 0.0;
    m_inParams.opacity.from = GlideConfig::inOpacity();
    m_inParams.opacity.to = 1.0;

    m_outParams.edge = static_cast<RotationEdge>(GlideConfig::outRotationEdge());
    m_outParams.angle.from = 0.0;
    m_outParams.angle.to = GlideConfig::outRotationAngle();
    m_outParams.distance.from = 0.0;
    m_outParams.distance.to = GlideConfig::outDistance();
    m_outParams.opacity.from = 1.0;
    m_outParams.opacity.to = GlideConfig::outOpacity();
}

void GlideEffect::prePaintScreen(effect::paint_data& data, std::chrono::milliseconds presentTime)
{
    auto animationIt = m_animations.begin();
    while (animationIt != m_animations.end()) {
        (*animationIt).timeLine.advance(presentTime);
        ++animationIt;
    }

    data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;

    effects->prePaintScreen(data, presentTime);
}

void GlideEffect::prePaintWindow(effect::window_prepaint_data& data,
                                 std::chrono::milliseconds presentTime)
{
    if (m_animations.contains(&data.window)) {
        data.paint.mask |= Effect::PAINT_WINDOW_TRANSFORMED;
    }

    effects->prePaintWindow(data, presentTime);
}

void GlideEffect::paintWindow(effect::window_paint_data& data)
{
    auto animationIt = m_animations.constFind(&data.window);
    if (animationIt == m_animations.constEnd()) {
        effects->paintWindow(data);
        return;
    }

    // Perspective projection distorts objects near edges
    // of the viewport. This is critical because distortions
    // near edges of the viewport are not desired with this effect.
    // To fix this, the center of the window will be moved to the origin,
    // after applying perspective projection, the center is moved back
    // to its "original" projected position. Overall, this is how the window
    // will be transformed:
    //  [move to the origin] -> [rotate] -> [translate] ->
    //    -> [perspective projection] -> [reverse "move to the origin"]
    auto const oldProjMatrix = data.paint.screen_projection_matrix;
    auto const windowGeo = data.window.frameGeometry();
    const QVector3D invOffset = oldProjMatrix.map(QVector3D(windowGeo.center()));
    QMatrix4x4 invOffsetMatrix;
    invOffsetMatrix.translate(invOffset.x(), invOffset.y());
    data.paint.projection_matrix = invOffsetMatrix * oldProjMatrix;

    // Move the center of the window to the origin.
    QPointF const offset
        = effects->renderTargetRect().center() - data.window.frameGeometry().center();
    data.paint.geo.translation += QVector3D(offset.x(), offset.y(), 0);

    auto const params = data.window.isDeleted() ? m_outParams : m_inParams;
    const qreal t = (*animationIt).timeLine.value();

    QVector3D const x_axis{1, 0, 0};
    QVector3D const y_axis{0, 1, 0};

    switch (params.edge) {
    case RotationEdge::Top:
        data.paint.geo.rotation.axis = x_axis;
        data.paint.geo.rotation.origin = QVector3D(0, 0, 0);
        data.paint.geo.rotation.angle = -interpolate(params.angle.from, params.angle.to, t);
        break;

    case RotationEdge::Right:
        data.paint.geo.rotation.axis = y_axis;
        data.paint.geo.rotation.origin = QVector3D(data.window.width(), 0, 0);
        data.paint.geo.rotation.angle = -interpolate(params.angle.from, params.angle.to, t);
        break;

    case RotationEdge::Bottom:
        data.paint.geo.rotation.axis = x_axis;
        data.paint.geo.rotation.origin = QVector3D(0, data.window.height(), 0);
        data.paint.geo.rotation.angle = interpolate(params.angle.from, params.angle.to, t);
        break;

    case RotationEdge::Left:
        data.paint.geo.rotation.axis = y_axis;
        data.paint.geo.rotation.origin = QVector3D(0, 0, 0);
        data.paint.geo.rotation.angle = interpolate(params.angle.from, params.angle.to, t);
        break;

    default:
        // Fallback to Top.
        data.paint.geo.rotation.axis = x_axis;
        data.paint.geo.rotation.origin = QVector3D(0, 0, 0);
        data.paint.geo.rotation.angle = -interpolate(params.angle.from, params.angle.to, t);
        break;
    }

    data.paint.geo.translation.setZ(-interpolate(params.distance.from, params.distance.to, t));
    data.paint.opacity *= interpolate(params.opacity.from, params.opacity.to, t);

    effects->paintWindow(data);
}

void GlideEffect::postPaintScreen()
{
    auto animationIt = m_animations.begin();
    while (animationIt != m_animations.end()) {
        if ((*animationIt).timeLine.done()) {
            animationIt = m_animations.erase(animationIt);
        } else {
            ++animationIt;
        }
    }

    effects->addRepaintFull();
    effects->postPaintScreen();
}

bool GlideEffect::isActive() const
{
    return !m_animations.isEmpty();
}

bool GlideEffect::supported()
{
    return effects->isOpenGLCompositing() && effects->animationsSupported();
}

void GlideEffect::windowAdded(EffectWindow* w)
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!isGlideWindow(w)) {
        return;
    }

    if (!w->isVisible()) {
        return;
    }

    const void* addGrab = w->data(WindowAddedGrabRole).value<void*>();
    if (addGrab && addGrab != this) {
        return;
    }

    w->setData(WindowAddedGrabRole, QVariant::fromValue(static_cast<void*>(this)));

    GlideAnimation& animation = m_animations[w];
    animation.timeLine.reset();
    animation.timeLine.setDirection(TimeLine::Forward);
    animation.timeLine.setDuration(m_duration);
    animation.timeLine.setEasingCurve(QEasingCurve::InCurve);

    effects->addRepaintFull();
}

void GlideEffect::windowClosed(EffectWindow* w)
{
    if (effects->activeFullScreenEffect()) {
        return;
    }

    if (!isGlideWindow(w)) {
        return;
    }

    if (!w->isVisible() || w->skipsCloseAnimation()) {
        return;
    }

    const void* closeGrab = w->data(WindowClosedGrabRole).value<void*>();
    if (closeGrab && closeGrab != this) {
        return;
    }

    w->setData(WindowClosedGrabRole, QVariant::fromValue(static_cast<void*>(this)));

    GlideAnimation& animation = m_animations[w];
    animation.deletedRef = EffectWindowDeletedRef(w);
    animation.visibleRef = EffectWindowVisibleRef(w, EffectWindow::PAINT_DISABLED_BY_DELETE);
    animation.timeLine.reset();
    animation.timeLine.setDirection(TimeLine::Forward);
    animation.timeLine.setDuration(m_duration);
    animation.timeLine.setEasingCurve(QEasingCurve::OutCurve);

    effects->addRepaintFull();
}

void GlideEffect::windowDeleted(EffectWindow* w)
{
    m_animations.remove(w);
}

void GlideEffect::windowDataChanged(EffectWindow* w, int role)
{
    if (role != WindowAddedGrabRole && role != WindowClosedGrabRole) {
        return;
    }

    if (w->data(role).value<void*>() == this) {
        return;
    }

    auto animationIt = m_animations.find(w);
    if (animationIt == m_animations.end()) {
        return;
    }

    m_animations.erase(animationIt);
}

bool GlideEffect::isGlideWindow(EffectWindow* w) const
{
    // We don't want to animate most of plasmashell's windows, yet, some
    // of them we want to, for example, Task Manager Settings window.
    // The problem is that all those window share single window class.
    // So, the only way to decide whether a window should be animated is
    // to use a heuristic: if a window has decoration, then it's most
    // likely a dialog or a settings window so we have to animate it.
    if (w->windowClass() == QLatin1String("plasmashell plasmashell")
        || w->windowClass() == QLatin1String("plasmashell org.kde.plasmashell")) {
        return w->hasDecoration();
    }

    if (s_blacklist.contains(w->windowClass())) {
        return false;
    }

    if (w->hasDecoration()) {
        return true;
    }

    // Don't animate combobox popups, tooltips, popup menus, etc.
    if (w->isPopupWindow()) {
        return false;
    }

    // Don't animate the outline and the screenlocker as it looks bad.
    if (w->isLockScreen() || w->isOutline()) {
        return false;
    }

    // Override-redirect windows are usually used for user interface
    // concepts that are not expected to be animated by this effect.
    if (w->isX11Client() && !w->isManaged()) {
        return false;
    }

    return w->isNormalWindow() || w->isDialog();
}

} // namespace KWin
