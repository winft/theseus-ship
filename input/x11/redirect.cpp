/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/tablet_redirect.h"
#include "input/touch_redirect.h"

// TODO(romangg): should only be included when KWIN_BUILD_TABBOX is defined.
#include "input/filters/tabbox.h"

#include "input/filters/decoration_event.h"
#include "input/filters/effects.h"
#include "input/filters/global_shortcut.h"
#include "input/filters/internal_window.h"
#include "input/filters/move_resize.h"
#include "input/filters/screen_edge.h"

#include "main.h"
#include "seat/session.h"

namespace KWin::input::x11
{

redirect::redirect()
    : input::redirect(new input::keyboard_redirect(this),
                      new input::pointer_redirect,
                      new input::tablet_redirect,
                      new input::touch_redirect)
{
}

void redirect::setupInputFilters()
{
    installInputEventFilter(new screen_edge_filter);

    installInputEventFilter(new effects_filter);
    installInputEventFilter(new move_resize_filter);

#ifdef KWIN_BUILD_TABBOX
    installInputEventFilter(new tabbox_filter);
#endif

    installInputEventFilter(new global_shortcut_filter);

    installInputEventFilter(new decoration_event_filter);
    installInputEventFilter(new internal_window_filter);
}

}
