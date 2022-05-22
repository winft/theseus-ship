/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event_filter.h"

namespace KWin::input
{

class redirect;

/**
 * Useful when there's no proper tablet support on the clients
 */
class fake_tablet_filter : public event_filter
{
public:
    explicit fake_tablet_filter(input::redirect& redirect);

    bool tabletToolEvent(QTabletEvent* event) override;

private:
    input::redirect& redirect;
};

}
