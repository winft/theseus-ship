/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SCREENEDGES_FILTER_H
#define KWIN_SCREENEDGES_FILTER_H

#include "base/x11/event_filter.h"

namespace KWin::render::backend::x11
{

class ScreenEdgesFilter : public platform::x11::event_filter
{
public:
    explicit ScreenEdgesFilter();

    bool event(xcb_generic_event_t* event) override;
};

}

#endif
