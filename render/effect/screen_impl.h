/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <kwineffects/effect_screen.h>

#include <QRect>

namespace KWin::render
{

template<typename Output>
class effect_screen_impl : public EffectScreen
{
public:
    explicit effect_screen_impl(Output* output, QObject* parent = nullptr)
        : EffectScreen(parent)
        , m_platformOutput(output)
    {
        using qout = typename decltype(output->qobject)::element_type;

        m_platformOutput->m_effectScreen = this;

        QObject::connect(output->qobject.get(), &qout::wake_up, this, &EffectScreen::wakeUp);
        QObject::connect(
            output->qobject.get(), &qout::about_to_turn_off, this, &EffectScreen::aboutToTurnOff);
        QObject::connect(output->qobject.get(),
                         &qout::scale_changed,
                         this,
                         &EffectScreen::devicePixelRatioChanged);
        QObject::connect(
            output->qobject.get(), &qout::geometry_changed, this, &EffectScreen::geometryChanged);
    }

    ~effect_screen_impl()
    {
        if (m_platformOutput) {
            m_platformOutput->m_effectScreen = nullptr;
        }
    }

    static effect_screen_impl* get(Output const* output)
    {
        return output->m_effectScreen;
    }

    Output* platformOutput() const
    {
        return m_platformOutput;
    }

    QString name() const override
    {
        return m_platformOutput->name();
    }

    qreal devicePixelRatio() const override
    {
        return m_platformOutput->scale();
    }

    QRect geometry() const override
    {
        return m_platformOutput->geometry();
    }

private:
    Output* m_platformOutput;
};

}
