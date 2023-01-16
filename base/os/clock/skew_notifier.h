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
#pragma once

#include "skew_notifier_engine.h"

#include "kwin_export.h"

#include <QObject>
#include <memory>

namespace KWin::base::os::clock
{

/**
 * The skew_notifier class provides a way for monitoring system clock changes.
 *
 * The skew_notifier class makes it possible to detect discontinuous changes to
 * the system clock. Such changes are usually initiated by the user adjusting values
 * in the Date and Time KCM or calls made to functions like settimeofday().
 */
class KWIN_EXPORT skew_notifier : public QObject
{
    Q_OBJECT
public:
    /**
     * Sets the active status of the clock skew notifier to @p active.
     *
     * clockSkewed() signal won't be emitted while the notifier is inactive.
     *
     * The notifier is inactive by default.
     */
    void set_active(bool active);

Q_SIGNALS:
    /**
     * This signal is emitted whenever the system clock is changed.
     */
    void skewed();

private:
    void load_engine();
    void unload_engine();

    std::unique_ptr<skew_notifier_engine> engine;
    bool is_active{false};
};

}
