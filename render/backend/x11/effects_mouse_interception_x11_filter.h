/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_EFFECTS_MOUSE_INTERCEPTION_X11_FILTER_H
#define KWIN_EFFECTS_MOUSE_INTERCEPTION_X11_FILTER_H

#include "base/x11/event_filter.h"

namespace KWin
{
class EffectsHandlerImpl;

namespace render::backend::x11
{

class EffectsMouseInterceptionX11Filter : public platform::x11::event_filter
{
public:
    explicit EffectsMouseInterceptionX11Filter(xcb_window_t window, EffectsHandlerImpl* effects);

    bool event(xcb_generic_event_t* event) override;

private:
    EffectsHandlerImpl* m_effects;
    xcb_window_t m_window;
};

}
}

#endif
