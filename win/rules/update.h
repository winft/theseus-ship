/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "win/types.h"
#include "win/window_qobject.h"

namespace KWin::win::rules
{

template<typename Ruling, typename RefWin>
bool update_rule(Ruling& ruling, RefWin const& ref_win, int selection)
{
    // TODO check this setting is for this client ?
    bool updated = false;

    auto remember = [selection](auto const& ruler, auto type) {
        return (selection & enum_index(type))
            && ruler.rule == static_cast<set_rule>(action::remember);
    };

    if (remember(ruling.above, type::above)) {
        updated = updated || ruling.above.data != ref_win.control->keep_above;
        ruling.above.data = ref_win.control->keep_above;
    }
    if (remember(ruling.below, type::below)) {
        updated = updated || ruling.below.data != ref_win.control->keep_below;
        ruling.below.data = ref_win.control->keep_below;
    }
    if (remember(ruling.desktop, type::desktop)) {
        updated = updated || ruling.desktop.data != ref_win.desktop();
        ruling.desktop.data = ref_win.desktop();
    }
    if (remember(ruling.desktopfile, type::desktop_file)) {
        auto const name = ref_win.control->desktop_file_name;
        updated = updated || ruling.desktopfile.data != name;
        ruling.desktopfile.data = name;
    }
    if (remember(ruling.fullscreen, type::fullscreen)) {
        updated = updated || ruling.fullscreen.data != ref_win.control->fullscreen;
        ruling.fullscreen.data = ref_win.control->fullscreen;
    }

    if (remember(ruling.maximizehoriz, type::maximize_horiz)) {
        updated = updated
            || ruling.maximizehoriz.data
                != flags(ref_win.maximizeMode() & maximize_mode::horizontal);
        ruling.maximizehoriz.data = flags(ref_win.maximizeMode() & maximize_mode::horizontal);
    }
    if (remember(ruling.maximizevert, type::maximize_vert)) {
        updated = updated
            || ruling.maximizevert.data != bool(ref_win.maximizeMode() & maximize_mode::vertical);
        ruling.maximizevert.data = flags(ref_win.maximizeMode() & maximize_mode::vertical);
    }
    if (remember(ruling.minimize, type::minimize)) {
        updated = updated || ruling.minimize.data != ref_win.control->minimized;
        ruling.minimize.data = ref_win.control->minimized;
    }
    if (remember(ruling.noborder, type::no_border)) {
        updated = updated || ruling.noborder.data != ref_win.noBorder();
        ruling.noborder.data = ref_win.noBorder();
    }

    if (remember(ruling.position, type::position)) {
        if (!ref_win.control->fullscreen) {
            auto new_pos = ruling.position.data;

            // Don't use the position in the direction which is maximized.
            if (!flags(ref_win.maximizeMode() & maximize_mode::horizontal)) {
                new_pos.setX(ref_win.pos().x());
            }
            if (!flags(ref_win.maximizeMode() & maximize_mode::vertical)) {
                new_pos.setY(ref_win.pos().y());
            }
            updated = updated || ruling.position.data != new_pos;
            ruling.position.data = new_pos;
        }
    }

    if (remember(ruling.screen, type::screen)) {
        int output_index = ref_win.central_output
            ? base::get_output_index(ref_win.space.base.outputs, *ref_win.central_output)
            : 0;
        updated = updated || ruling.screen.data != output_index;
        ruling.screen.data = output_index;
    }
    if (remember(ruling.size, type::size)) {
        if (!ref_win.control->fullscreen) {
            auto new_size = ruling.size.data;
            // don't use the position in the direction which is maximized
            if (!flags(ref_win.maximizeMode() & maximize_mode::horizontal)) {
                new_size.setWidth(ref_win.size().width());
            }
            if (!flags(ref_win.maximizeMode() & maximize_mode::vertical)) {
                new_size.setHeight(ref_win.size().height());
            }
            updated = updated || ruling.size.data != new_size;
            ruling.size.data = new_size;
        }
    }
    if (remember(ruling.skippager, type::skip_pager)) {
        updated = updated || ruling.skippager.data != ref_win.control->skip_pager();
        ruling.skippager.data = ref_win.control->skip_pager();
    }
    if (remember(ruling.skipswitcher, type::skip_switcher)) {
        updated = updated || ruling.skipswitcher.data != ref_win.control->skip_switcher();
        ruling.skipswitcher.data = ref_win.control->skip_switcher();
    }
    if (remember(ruling.skiptaskbar, type::skip_taskbar)) {
        updated = updated || ruling.skiptaskbar.data != ref_win.control->skip_taskbar();
        ruling.skiptaskbar.data = ref_win.control->skip_taskbar();
    }

    return updated;
}

template<typename RuleWin, typename RefWin>
void update_window(RuleWin& rule_win, RefWin& ref_win, int selection)
{
    auto updated{false};

    for (auto&& rule : rule_win.rules) {
        if (update_rule(*rule, ref_win, selection)) {
            // no short-circuiting here
            updated = true;
        }
    }

    if (updated) {
        ref_win.space.rule_book->requestDiskStorage();
    }
}

}
