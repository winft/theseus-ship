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

void Effect::prePaintScreen(effect::screen_prepaint_data& data)
{
    effects->prePaintScreen(data);
}

void Effect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);
}

void Effect::postPaintScreen()
{
    effects->postPaintScreen();
}

void Effect::prePaintWindow(effect::window_prepaint_data& data)
{
    effects->prePaintWindow(data);
}

void Effect::paintWindow(effect::window_paint_data& data)
{
    effects->paintWindow(data);
}

void Effect::postPaintWindow(EffectWindow* w)
{
    effects->postPaintWindow(w);
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

void Effect::drawWindow(effect::window_paint_data& data)
{
    effects->drawWindow(data);
}

void Effect::buildQuads(EffectWindow* w, WindowQuadList& quadList)
{
    effects->buildQuads(w, quadList);
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

uint32_t Effect::x11RootWindow() const
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

}
