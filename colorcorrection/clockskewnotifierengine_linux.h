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

#include "clockskewnotifierengine_p.h"

namespace KWin::base::os::clock
{

class linux_skew_notifier_engine : public skew_notifier_engine
{
    Q_OBJECT

public:
    linux_skew_notifier_engine(int fd);
    ~linux_skew_notifier_engine() override;

    static std::unique_ptr<linux_skew_notifier_engine> create();

private Q_SLOTS:
    void handle_timer_cancelled();

private:
    int m_fd;
};

}
