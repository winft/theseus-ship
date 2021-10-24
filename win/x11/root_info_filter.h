/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

namespace KWin::win::x11
{
class RootInfo;

class root_info_filter : public base::x11::event_filter
{
public:
    explicit root_info_filter(RootInfo* info);

    bool event(xcb_generic_event_t* event) override;

private:
    win::x11::RootInfo* root_info;
};

}
