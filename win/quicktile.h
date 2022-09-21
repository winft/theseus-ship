/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo_restrict.h"
#include "types.h"

namespace KWin::win
{

template<typename Win>
void set_maximize(Win* win, bool vertically, bool horizontally)
{
    auto mode = maximize_mode::restore;
    if (vertically) {
        mode |= maximize_mode::vertical;
    }
    if (horizontally) {
        mode |= maximize_mode::horizontal;
    }
    win->update_maximized(mode);
}

/**
 * Checks if the mouse cursor is near the edge of the screen and if so
 * activates quick tiling or maximization.
 */
template<typename Win>
void check_quicktile_maximization_zones(Win* win, int xroot, int yroot)
{
    auto mode = quicktiles::none;
    bool inner_border = false;
    auto const& outputs = win->space.base.outputs;

    for (size_t i = 0; i < outputs.size(); ++i) {
        if (!outputs.at(i)->geometry().contains(QPoint(xroot, yroot))) {
            continue;
        }

        auto in_screen = [i, &outputs](const QPoint& pt) {
            for (size_t j = 0; j < outputs.size(); ++j) {
                if (j != i && outputs.at(j)->geometry().contains(pt)) {
                    return true;
                }
            }
            return false;
        };

        auto area
            = space_window_area(win->space, MaximizeArea, QPoint(xroot, yroot), win->desktop());
        if (kwinApp()->options->qobject->electricBorderTiling()) {
            if (xroot <= area.x() + 20) {
                mode |= quicktiles::left;
                inner_border = in_screen(QPoint(area.x() - 1, yroot));
            } else if (xroot >= area.x() + area.width() - 20) {
                mode |= quicktiles::right;
                inner_border = in_screen(QPoint(area.right() + 1, yroot));
            }
        }

        if (mode != quicktiles::none) {
            auto ratio = kwinApp()->options->qobject->electricBorderCornerRatio();
            if (yroot <= area.y() + area.height() * ratio) {
                mode |= quicktiles::top;
            } else if (yroot >= area.y() + area.height() - area.height() * ratio) {
                mode |= quicktiles::bottom;
            }
        } else if (kwinApp()->options->qobject->electricBorderMaximize() && yroot <= area.y() + 5
                   && win->isMaximizable()) {
            mode = quicktiles::maximize;
            inner_border = in_screen(QPoint(xroot, area.y() - 1));
        }
        break;
    }
    if (mode != win->control->electric) {
        set_electric(win, mode);
        if (inner_border) {
            delayed_electric_maximize(win);
        } else {
            set_electric_maximizing(win, mode != quicktiles::none);
        }
    }
}

/**
 * Sets the quick tile mode ("snap") of this window.
 * This will also handle preserving and restoring of window geometry as necessary.
 * @param mode The tile mode (left/right) to give this window.
 * @param keyboard Defines whether to take keyboard cursor into account.
 */
template<typename Win>
void set_quicktile_mode(Win* win, quicktiles mode, bool keyboard)
{
    // Only allow quick tile on a regular window.
    if (!win->isResizable()) {
        return;
    }
    if (is_applet_popup(win)) {
        return;
    }

    win->space.focusMousePos = win->space.input->cursor->pos();

    geometry_updates_blocker blocker(win);

    // Store current geometry if not already defined.
    if (!win->geo.restore.max.isValid()) {
        win->geo.restore.max = win->geo.frame;
    }

    // Later calls to set_maximize(..) would reset the restore geometry.
    auto const old_restore_geo = win->geo.restore.max;

    if (mode == quicktiles::maximize) {
        // Special case where we just maximize and return early.

        auto const old_quicktiling = win->control->quicktiling;
        win->control->quicktiling = quicktiles::none;

        if (win->maximizeMode() == maximize_mode::full) {
            // TODO(romangg): When window was already maximized we now "unmaximize" it. Why?
            set_maximize(win, false, false);
        } else {
            win->control->quicktiling = quicktiles::maximize;
            set_maximize(win, true, true);
            auto clientArea = space_window_area(win->space, MaximizeArea, win);

            if (auto frame_geo = pending_frame_geometry(win); frame_geo.top() != clientArea.top()) {
                frame_geo.moveTop(clientArea.top());
                win->setFrameGeometry(frame_geo);
            }
            win->geo.restore.max = old_restore_geo;
        }

        if (old_quicktiling != win->control->quicktiling) {
            Q_EMIT win->qobject->quicktiling_changed();
        }
        return;
    }

    // Sanitize the mode, ie. simplify "invalid" combinations.
    if ((mode & quicktiles::horizontal) == quicktiles::horizontal) {
        mode &= ~quicktiles::horizontal;
    }
    if ((mode & quicktiles::vertical) == quicktiles::vertical) {
        mode &= ~quicktiles::vertical;
    }

    // Used by electric_border_maximize_geometry(..).
    win->control->electric = mode;

    if (win->geo.update.max_mode != maximize_mode::restore) {
        // Restore from maximized so that it is possible to tile maximized windows with one hit or
        // by dragging.
        if (mode != quicktiles::none) {
            // Temporary, so the maximize code doesn't get all confused
            win->control->quicktiling = quicktiles::none;

            set_maximize(win, false, false);

            auto ref_pos
                = keyboard ? pending_frame_geometry(win).center() : win->space.input->cursor->pos();

            win->setFrameGeometry(electric_border_maximize_geometry(win, ref_pos, win->desktop()));
            // Store the mode change
            win->control->quicktiling = mode;
            win->geo.restore.max = old_restore_geo;
        } else {
            win->control->quicktiling = mode;
            set_maximize(win, false, false);
        }

        Q_EMIT win->qobject->quicktiling_changed();
        return;
    }

    if (mode != quicktiles::none) {
        auto target_screen
            = keyboard ? pending_frame_geometry(win).center() : win->space.input->cursor->pos();

        if (win->control->quicktiling == mode) {
            // If trying to tile to the side that the window is already tiled to move the window to
            // the next screen if it exists, otherwise toggle the mode (set quicktiles::none)

            // TODO(romangg): Once we use size_t consistently for screens identification replace
            // these (currentyl implicit casted) types with auto.
            auto const& outputs = win->space.base.outputs;
            auto const old_screen = win->topo.central_output
                ? base::get_output_index(outputs, *win->topo.central_output)
                : 0;
            auto screen = old_screen;

            std::vector<QRect> screens_geos(outputs.size());
            screens_geos.resize(outputs.size());

            for (size_t i = 0; i < outputs.size(); ++i) {
                // Geometry cache.
                screens_geos[i] = outputs.at(i)->geometry();
            }

            for (size_t i = 0; i < outputs.size(); ++i) {
                if (i == old_screen) {
                    continue;
                }

                if (screens_geos[i].bottom() <= screens_geos[old_screen].top()
                    || screens_geos[i].top() >= screens_geos[old_screen].bottom()) {
                    // Not in horizontal line
                    continue;
                }

                auto const x = screens_geos[i].center().x();
                if ((mode & quicktiles::horizontal) == quicktiles::left) {
                    if (x >= screens_geos[old_screen].center().x()
                        || (old_screen != screen && x <= screens_geos[screen].center().x())) {
                        // Not left of current or more left then found next
                        continue;
                    }
                } else if ((mode & quicktiles::horizontal) == quicktiles::right) {
                    if (x <= screens_geos[old_screen].center().x()
                        || (old_screen != screen && x >= screens_geos[screen].center().x())) {
                        // Not right of current or more right then found next.
                        continue;
                    }
                }

                screen = i;
            }

            if (screen == old_screen) {
                // No other screens, toggle tiling.
                mode = quicktiles::none;
            } else {
                // Move to other screen
                win->setFrameGeometry(win->geo.restore.max.translated(
                    screens_geos[screen].topLeft() - screens_geos[old_screen].topLeft()));
                target_screen = screens_geos[screen].center();

                // Swap sides
                if (flags(mode & quicktiles::horizontal)) {
                    mode = (~mode & quicktiles::horizontal) | (mode & quicktiles::vertical);
                }
            }

            // used by electric_border_maximize_geometry(.)
            set_electric(win, mode);

        } else if (win->control->quicktiling == quicktiles::none) {
            // Not coming out of an existing tile, not shifting monitors, we're setting a brand new
            // tile. Store geometry first, so we can go out of this tile later.
            if (!win->geo.restore.max.isValid()) {
                win->geo.restore.max = win->geo.frame;
            }
        }

        if (mode != quicktiles::none) {
            win->control->quicktiling = mode;
            // Temporary, so the maximize code doesn't get all confused
            win->control->quicktiling = quicktiles::none;

            // TODO(romangg): With decorations this was previously forced in order to handle borders
            //                being changed. Is it safe to do this now without that?
            win->setFrameGeometry(
                electric_border_maximize_geometry(win, target_screen, win->desktop()));
        }

        // Store the mode change
        win->control->quicktiling = mode;
    }

    if (mode == quicktiles::none) {
        win->control->quicktiling = quicktiles::none;
        win->setFrameGeometry(win->geo.restore.max);

        // Just in case it's a different screen
        check_workspace_position(win);

        // If we're here we can unconditionally reset the restore geometry since we earlier excluded
        // the case of the window being maximized.
        win->geo.restore.max = QRect();
    }

    Q_EMIT win->qobject->quicktiling_changed();
}

}
