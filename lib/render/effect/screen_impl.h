/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

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

    virtual int refreshRate() const override
    {
        return m_platformOutput->refresh_rate();
    }

    Transform transform() const override
    {
        // TODO(romangg): get correct value.
        return EffectScreen::Transform::Normal;
    }

    QString manufacturer() const override
    {
        return m_platformOutput->manufacturer();
    }

    QString model() const override
    {
        return m_platformOutput->model();
    }

    QString serialNumber() const override
    {
        return m_platformOutput->serial_number();
    }

private:
    Output* m_platformOutput;
};

template<typename Effects, typename Output>
effect_screen_impl<Output>* get_effect_screen(Effects const& effects, Output const& output)
{
    auto const& screens = effects.screens();
    for (auto&& eff_screen : qAsConst(screens)) {
        auto eff_screen_impl = static_cast<effect_screen_impl<Output>*>(eff_screen);
        if (&output == eff_screen_impl->platformOutput()) {
            return eff_screen_impl;
        }
    }
    return nullptr;
}

}
