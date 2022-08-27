/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_impl.h"

#include "base/output.h"

namespace KWin::render
{

effect_screen_impl::effect_screen_impl(base::output* output, QObject* parent)
    : EffectScreen(parent)
    , m_platformOutput(output)
{
    QObject::connect(
        output->qobject.get(), &base::output_qobject::wake_up, this, &EffectScreen::wakeUp);
    QObject::connect(output->qobject.get(),
                     &base::output_qobject::about_to_turn_off,
                     this,
                     &EffectScreen::aboutToTurnOff);
    QObject::connect(output->qobject.get(),
                     &base::output_qobject::scale_changed,
                     this,
                     &EffectScreen::devicePixelRatioChanged);
    QObject::connect(output->qobject.get(),
                     &base::output_qobject::geometry_changed,
                     this,
                     &EffectScreen::geometryChanged);
}

base::output* effect_screen_impl::platformOutput() const
{
    return m_platformOutput;
}

QString effect_screen_impl::name() const
{
    return m_platformOutput->name();
}

qreal effect_screen_impl::devicePixelRatio() const
{
    return m_platformOutput->scale();
}

QRect effect_screen_impl::geometry() const
{
    return m_platformOutput->geometry();
}

}
