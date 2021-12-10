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
#include "skew_notifier_engine.h"

#if defined(Q_OS_LINUX)
#include "linux_skew_notifier_engine.h"
#endif

namespace KWin::base::os::clock
{

std::unique_ptr<skew_notifier_engine> skew_notifier_engine::create()
{
#if defined(Q_OS_LINUX)
    return linux_skew_notifier_engine::create();
#else
    return nullptr;
#endif
}

}
