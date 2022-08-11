/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <kwineffects/effect_screen.h>

namespace KWin
{

namespace base
{
class output;
}

namespace render
{

class effect_screen_impl : public EffectScreen
{
public:
    explicit effect_screen_impl(base::output* output, QObject* parent = nullptr);

    base::output* platformOutput() const;

    QString name() const override;
    qreal devicePixelRatio() const override;
    QRect geometry() const override;

private:
    base::output* m_platformOutput;
};

}
}
