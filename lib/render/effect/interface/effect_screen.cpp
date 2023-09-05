/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect_screen.h"

#include <QRect>

namespace KWin
{

EffectScreen::EffectScreen(QObject* parent)
    : QObject(parent)
{
}

QPointF EffectScreen::mapToGlobal(const QPointF& pos) const
{
    return pos + geometry().topLeft();
}

QPointF EffectScreen::mapFromGlobal(const QPointF& pos) const
{
    return pos - geometry().topLeft();
}

}
