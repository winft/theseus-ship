/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect.h"

#include "effect_window.h"
#include "effects_handler.h"
#include "paint_data.h"

#include <KConfigGroup>

namespace KWin
{

Effect::Effect(QObject* parent)
    : QObject(parent)
{
}

Effect::~Effect()
{
}

void Effect::reconfigure(ReconfigureFlags)
{
}

void* Effect::proxy()
{
    return nullptr;
}

void Effect::windowInputMouseEvent(QEvent*)
{
}

void Effect::grabbedKeyboardEvent(QKeyEvent*)
{
}

bool Effect::borderActivated(ElectricBorder)
{
    return false;
}

void Effect::prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    effects->prePaintScreen(data, presentTime);
}

void Effect::paintScreen(int mask, const QRegion& region, ScreenPaintData& data)
{
    effects->paintScreen(mask, region, data);
}

void Effect::postPaintScreen()
{
    effects->postPaintScreen();
}

void Effect::prePaintWindow(EffectWindow* w,
                            WindowPrePaintData& data,
                            std::chrono::milliseconds presentTime)
{
    effects->prePaintWindow(w, data, presentTime);
}

void Effect::paintWindow(EffectWindow* w, int mask, QRegion region, WindowPaintData& data)
{
    effects->paintWindow(w, mask, region, data);
}

void Effect::postPaintWindow(EffectWindow* w)
{
    effects->postPaintWindow(w);
}

void Effect::paintEffectFrame(KWin::EffectFrame* frame,
                              const QRegion& region,
                              double opacity,
                              double frameOpacity)
{
    effects->paintEffectFrame(frame, region, opacity, frameOpacity);
}

bool Effect::provides(Feature)
{
    return false;
}

bool Effect::isActive() const
{
    return true;
}

QString Effect::debug(const QString&) const
{
    return QString();
}

void Effect::drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data)
{
    effects->drawWindow(w, mask, region, data);
}

void Effect::buildQuads(EffectWindow* w, WindowQuadList& quadList)
{
    effects->buildQuads(w, quadList);
}

void Effect::setPositionTransformations(WindowPaintData& data,
                                        QRect& region,
                                        EffectWindow* w,
                                        const QRect& r,
                                        Qt::AspectRatioMode aspect)
{
    QSize size = w->size();
    size.scale(r.size(), aspect);
    data.setXScale(size.width() / double(w->width()));
    data.setYScale(size.height() / double(w->height()));
    int width = int(w->width() * data.xScale());
    int height = int(w->height() * data.yScale());
    int x = r.x() + (r.width() - width) / 2;
    int y = r.y() + (r.height() - height) / 2;
    region = QRect(x, y, width, height);
    data.setXTranslation(x - w->x());
    data.setYTranslation(y - w->y());
}

QPoint Effect::cursorPos()
{
    return effects->cursorPos();
}

double Effect::animationTime(const KConfigGroup& cfg, const QString& key, int defaultTime)
{
    int time = cfg.readEntry(key, 0);
    return time != 0 ? time : qMax(defaultTime * effects->animationTimeFactor(), 1.);
}

double Effect::animationTime(int defaultTime)
{
    // at least 1ms, otherwise 0ms times can break some things
    return qMax(defaultTime * effects->animationTimeFactor(), 1.);
}

int Effect::requestedEffectChainPosition() const
{
    return 0;
}

xcb_connection_t* Effect::xcbConnection() const
{
    return effects->xcbConnection();
}

xcb_window_t Effect::x11RootWindow() const
{
    return effects->x11RootWindow();
}

bool Effect::touchDown(qint32 /*id*/, const QPointF& /*pos*/, quint32 /*time*/)
{
    return false;
}

bool Effect::touchMotion(qint32 /*id*/, const QPointF& /*pos*/, quint32 /*time*/)
{
    return false;
}

bool Effect::touchUp(qint32 /*id*/, quint32 /*time*/)
{
    return false;
}

bool Effect::perform(Feature /*feature*/, const QVariantList& /*arguments*/)
{
    return false;
}

KSharedConfigPtr Effect::get_config()
{
    return effects->config();
}

}
