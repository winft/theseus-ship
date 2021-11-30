/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_EFFECTS_MOUSE_INTERCEPTION_X11_FILTER_H
#define KWIN_EFFECTS_MOUSE_INTERCEPTION_X11_FILTER_H

#include "base/x11/event_filter.h"

namespace KWin::render
{
class effects_handler_impl;

namespace backend::x11
{

class EffectsMouseInterceptionX11Filter : public base::x11::event_filter
{
public:
    explicit EffectsMouseInterceptionX11Filter(xcb_window_t window, effects_handler_impl* effects);

    bool event(xcb_generic_event_t* event) override;

private:
    effects_handler_impl* m_effects;
    xcb_window_t m_window;
};

}
}

#endif
