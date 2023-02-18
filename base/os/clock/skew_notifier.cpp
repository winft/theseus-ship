/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "skew_notifier.h"

namespace KWin::base::os::clock
{

void skew_notifier::load_engine()
{
    engine = skew_notifier_engine::create();

    if (engine) {
        QObject::connect(engine.get(), &skew_notifier_engine::skewed, this, &skew_notifier::skewed);
    }
}

void skew_notifier::unload_engine()
{
    if (!engine) {
        return;
    }

    QObject::disconnect(engine.get(), &skew_notifier_engine::skewed, this, &skew_notifier::skewed);
    engine->deleteLater();

    engine = nullptr;
}

void skew_notifier::set_active(bool set)
{
    if (is_active == set) {
        return;
    }

    is_active = set;

    if (is_active) {
        load_engine();
    } else {
        unload_engine();
    }
}

}
