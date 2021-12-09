/*
 * Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "clockskewnotifier.h"

namespace KWin
{

void ClockSkewNotifier::load_engine()
{
    engine = ClockSkewNotifierEngine::create();

    if (engine) {
        QObject::connect(
            engine.get(), &ClockSkewNotifierEngine::skewed, this, &ClockSkewNotifier::skewed);
    }
}

void ClockSkewNotifier::unload_engine()
{
    if (!engine) {
        return;
    }

    QObject::disconnect(
        engine.get(), &ClockSkewNotifierEngine::skewed, this, &ClockSkewNotifier::skewed);
    engine->deleteLater();

    engine = nullptr;
}

void ClockSkewNotifier::set_active(bool set)
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
