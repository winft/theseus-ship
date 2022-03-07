/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

namespace KWin::render::x11
{

class effects_handler_impl;

class mouse_intercept_filter : public base::x11::event_filter
{
public:
    explicit mouse_intercept_filter(xcb_window_t window, effects_handler_impl* effects);

    bool event(xcb_generic_event_t* event) override;

private:
    effects_handler_impl* m_effects;
    xcb_window_t m_window;
};

}
