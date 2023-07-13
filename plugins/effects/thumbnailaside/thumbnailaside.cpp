/*
SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "thumbnailaside.h"

// KConfigSkeleton
#include "thumbnailasideconfig.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>

#include <KLocalizedString>
#include <QAction>
#include <QMatrix4x4>

namespace KWin
{

ThumbnailAsideEffect::ThumbnailAsideEffect()
{
    initConfig<ThumbnailAsideConfig>();
    QAction* a = new QAction(this);
    a->setObjectName(QStringLiteral("ToggleCurrentThumbnail"));
    a->setText(i18n("Toggle Thumbnail for Current Window"));
    effects->registerGlobalShortcutAndDefault({Qt::META | Qt::CTRL | Qt::Key_T}, a);
    connect(a, &QAction::triggered, this, &ThumbnailAsideEffect::toggleCurrentThumbnail);

    connect(effects, &EffectsHandler::windowClosed, this, &ThumbnailAsideEffect::slotWindowClosed);
    connect(effects,
            &EffectsHandler::windowFrameGeometryChanged,
            this,
            &ThumbnailAsideEffect::slotWindowFrameGeometryChanged);
    connect(
        effects, &EffectsHandler::windowDamaged, this, &ThumbnailAsideEffect::slotWindowDamaged);
    connect(
        effects, &EffectsHandler::screenLockingChanged, this, &ThumbnailAsideEffect::repaintAll);
    reconfigure(ReconfigureAll);
}

void ThumbnailAsideEffect::reconfigure(ReconfigureFlags)
{
    ThumbnailAsideConfig::self()->read();
    maxwidth = ThumbnailAsideConfig::maxWidth();
    spacing = ThumbnailAsideConfig::spacing();
    opacity = ThumbnailAsideConfig::opacity() / 100.0;
    screen = ThumbnailAsideConfig::screen(); // Xinerama screen TODO add gui option
    arrange();
}

/** Helper to set WindowPaintData and QRegion to necessary transformations so that
 * a following drawWindow() would put the window at the requested geometry (useful for
 * thumbnails)
 */
static void setPositionTransformations(effect::window_paint_data& data,
                                       QRect const& r,
                                       Qt::AspectRatioMode aspect)
{
    auto size = data.window.size();
    size.scale(r.size(), aspect);

    data.paint.geo.scale.setX(size.width() / double(data.window.width()));
    data.paint.geo.scale.setY(size.height() / double(data.window.height()));

    auto width = static_cast<int>(data.window.width() * data.paint.geo.scale.x());
    auto height = static_cast<int>(data.window.height() * data.paint.geo.scale.y());
    int x = r.x() + (r.width() - width) / 2;
    int y = r.y() + (r.height() - height) / 2;

    data.paint.region = QRect(x, y, width, height);
    data.paint.geo.translation.setX(x - data.window.x());
    data.paint.geo.translation.setY(y - data.window.y());
}

void ThumbnailAsideEffect::paintScreen(effect::screen_paint_data& data)
{
    painted = QRegion();
    effects->paintScreen(data);

    auto const projectionMatrix = data.paint.projection_matrix;
    for (auto const& d : qAsConst(windows)) {
        if (painted.intersects(d.rect)) {
            effect::window_paint_data win_data{
                *d.window,
                {
                    .mask = PAINT_WINDOW_OPAQUE | PAINT_WINDOW_TRANSLUCENT
                        | PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_LANCZOS,
                    .projection_matrix = projectionMatrix,
                }};
            setPositionTransformations(win_data, d.rect, Qt::KeepAspectRatio);
            win_data.paint.opacity = d.window->opacity() * opacity;
            effects->drawWindow(win_data);
        }
    }
}

void ThumbnailAsideEffect::paintWindow(effect::window_paint_data& data)
{
    effects->paintWindow(data);
    painted |= data.paint.region;
}

void ThumbnailAsideEffect::slotWindowDamaged(EffectWindow* w, QRegion const&)
{
    for (auto const& d : qAsConst(windows)) {
        if (d.window == w)
            effects->addRepaint(d.rect);
    }
}

void ThumbnailAsideEffect::slotWindowFrameGeometryChanged(EffectWindow* w, const QRect& old)
{
    for (auto const& d : qAsConst(windows)) {
        if (d.window == w) {
            if (w->size() == old.size())
                effects->addRepaint(d.rect);
            else
                arrange();
            return;
        }
    }
}

void ThumbnailAsideEffect::slotWindowClosed(EffectWindow* w)
{
    removeThumbnail(w);
}

void ThumbnailAsideEffect::toggleCurrentThumbnail()
{
    EffectWindow* active = effects->activeWindow();
    if (active == nullptr)
        return;
    if (windows.contains(active))
        removeThumbnail(active);
    else
        addThumbnail(active);
}

void ThumbnailAsideEffect::addThumbnail(EffectWindow* w)
{
    repaintAll(); // repaint old areas
    Data d;
    d.window = w;
    d.index = windows.count();
    windows[w] = d;
    arrange();
}

void ThumbnailAsideEffect::removeThumbnail(EffectWindow* w)
{
    if (!windows.contains(w))
        return;
    repaintAll(); // repaint old areas
    int index = windows[w].index;
    windows.remove(w);
    for (QHash<EffectWindow*, Data>::Iterator it = windows.begin(); it != windows.end(); ++it) {
        Data& d = *it;
        if (d.index > index)
            --d.index;
    }
    arrange();
}

void ThumbnailAsideEffect::arrange()
{
    if (windows.size() == 0)
        return;
    int height = 0;
    QVector<int> pos(windows.size());
    int mwidth = 0;
    for (auto const& d : qAsConst(windows)) {
        height += d.window->height();
        mwidth = qMax(mwidth, d.window->width());
        pos[d.index] = d.window->height();
    }
    EffectScreen* effectiveScreen = effects->findScreen(screen);
    if (!effectiveScreen) {
        effectiveScreen = effects->activeScreen();
    }
    QRect area = effects->clientArea(MaximizeArea, effectiveScreen, effects->currentDesktop());
    double scale = area.height() / double(height);
    scale = qMin(scale, maxwidth / double(mwidth)); // don't be wider than maxwidth pixels
    int add = 0;
    for (int i = 0; i < windows.size(); ++i) {
        pos[i] = int(pos[i] * scale);
        pos[i] += spacing + add; // compute offset of each item
        add = pos[i];
    }
    for (QHash<EffectWindow*, Data>::Iterator it = windows.begin(); it != windows.end(); ++it) {
        Data& d = *it;
        int width = int(d.window->width() * scale);
        d.rect = QRect(area.right() - width,
                       area.bottom() - pos[d.index],
                       width,
                       int(d.window->height() * scale));
    }
    repaintAll();
}

void ThumbnailAsideEffect::repaintAll()
{
    for (auto const& d : qAsConst(windows)) {
        effects->addRepaint(d.rect);
    }
}

bool ThumbnailAsideEffect::isActive() const
{
    return !windows.isEmpty() && !effects->isScreenLocked();
}

} // namespace
