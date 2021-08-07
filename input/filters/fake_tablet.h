/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

namespace KWin::input
{

/**
 * Useful when there's no proper tablet support on the clients
 */
class fake_tablet_filter : public event_filter
{
public:
    bool tabletToolEvent(QTabletEvent* event) override;
};

}
