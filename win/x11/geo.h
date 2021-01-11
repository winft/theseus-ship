/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "scene.h"

#include "win/setup.h"
#include "win/space.h"

#include <xcb/xcb_icccm.h>

namespace KWin::win::x11
{

template<typename Win>
void update_shape(Win* win)
{
    if (win->shape()) {
        // Workaround for #19644 - Shaped windows shouldn't have decoration
        if (!win->app_no_border) {
            // Only when shape is detected for the first time, still let the user to override
            win->app_no_border = true;
            win->user_no_border = win->control->rules().checkNoBorder(true);
            win->updateDecoration(true);
        }
        if (win->noBorder()) {
            auto const client_pos = frame_relative_client_rect(win).topLeft();
            xcb_shape_combine(connection(),
                              XCB_SHAPE_SO_SET,
                              XCB_SHAPE_SK_BOUNDING,
                              XCB_SHAPE_SK_BOUNDING,
                              win->frameId(),
                              client_pos.x(),
                              client_pos.y(),
                              win->xcb_window());
        }
    } else if (win->app_no_border) {
        xcb_shape_mask(connection(),
                       XCB_SHAPE_SO_SET,
                       XCB_SHAPE_SK_BOUNDING,
                       win->frameId(),
                       0,
                       0,
                       XCB_PIXMAP_NONE);
        detect_no_border(win);
        win->app_no_border = win->user_no_border;
        win->user_no_border = win->control->rules().checkNoBorder(win->user_no_border
                                                                  || win->motif_hints.noBorder());
        win->updateDecoration(true);
    }

    // Decoration mask (i.e. 'else' here) setting is done in setMask()
    // when the decoration calls it or when the decoration is created/destroyed
    win->update_input_shape();
    if (win::compositing()) {
        win->addRepaintFull();

        // In case shape change removes part of this window
        win->addWorkspaceRepaint(win::visible_rect(win));
    }
    Q_EMIT win->geometryShapeChanged(win, win->frameGeometry());
}

template<typename Win>
void set_shade(Win* win, win::shade mode)
{
    if (mode == win::shade::hover && win::is_move(win)) {
        // causes geometry breaks and is probably nasty
        return;
    }

    if (win::is_special_window(win) || win->noBorder()) {
        mode = win::shade::none;
    }

    mode = win->control->rules().checkShade(mode);
    if (win->shade_mode == mode) {
        return;
    }

    auto was_shade = win::shaded(win);
    auto was_shade_mode = win->shade_mode;
    win->shade_mode = mode;

    if (was_shade == win::shaded(win)) {
        // Decoration may want to update after e.g. hover-shade changes
        Q_EMIT win->shadeChanged();

        // No real change in shaded state
        return;
    }

    // noborder windows can't be shaded
    assert(win::decoration(win));

    win::geometry_updates_blocker blocker(win);

    // TODO: All this unmapping, resizing etc. feels too much duplicated from elsewhere
    if (win::shaded(win)) {
        win->addWorkspaceRepaint(win::visible_rect(win));

        // Shade
        win->restore_geometries.shade = win->frameGeometry();

        QSize s(win->sizeForClientSize(QSize(frame_to_client_size(win, win->size()))));
        s.setHeight(win::top_border(win) + win::bottom_border(win));

        // Avoid getting UnmapNotify
        win->xcb_windows.wrapper.selectInput(ClientWinMask);

        win->xcb_windows.wrapper.unmap();
        win->xcb_windows.client.unmap();

        win->xcb_windows.wrapper.selectInput(ClientWinMask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
        export_mapping_state(win, XCB_ICCCM_WM_STATE_ICONIC);
        plain_resize(win, s);

        if (was_shade_mode == win::shade::hover) {
            if (win->shade_below && index_of(workspace()->stackingOrder(), win->shade_below) > -1) {
                workspace()->restack(win, win->shade_below, true);
            }
            if (win->control->active()) {
                workspace()->activateNextClient(win);
            }
        } else if (win->control->active()) {
            workspace()->focusToNull();
        }
    } else {
        if (auto deco_client = win->control->deco().client) {
            deco_client->signalShadeChange();
        }

        plain_resize(win, win->restore_geometries.shade.size());
        win->restore_geometries.maximize = win->frameGeometry();

        if ((win->shade_mode == win::shade::hover || win->shade_mode == win::shade::activated)
            && win->control->rules().checkAcceptFocus(win->info->input())) {
            win::set_active(win, true);
        }

        if (win->shade_mode == win::shade::hover) {
            auto order = workspace()->stackingOrder();
            // invalidate, since "win" could be the topmost toplevel and shade_below dangeling
            win->shade_below = nullptr;
            // this is likely related to the index parameter?!
            for (size_t idx = index_of(order, win) + 1; idx < order.size(); ++idx) {
                win->shade_below = qobject_cast<Win*>(order.at(idx));
                if (win->shade_below) {
                    break;
                }
            }

            if (win->shade_below && win::is_normal(win->shade_below)) {
                workspace()->raise_window(win);
            } else {
                win->shade_below = nullptr;
            }
        }

        win->xcb_windows.wrapper.map();
        win->xcb_windows.client.map();

        export_mapping_state(win, XCB_ICCCM_WM_STATE_NORMAL);
        if (win->control->active()) {
            workspace()->request_focus(win);
        }
    }

    win->info->setState(win::shaded(win) ? NET::Shaded : NET::States(), NET::Shaded);
    win->info->setState(win->isShown(false) ? NET::States() : NET::Hidden, NET::Hidden);

    win->discardWindowPixmap();
    update_visibility(win);
    update_allowed_actions(win);
    win->updateWindowRules(Rules::Shade);

    Q_EMIT win->shadeChanged();
}

template<typename Win>
QRect frame_rect_to_buffer_rect(Win* win, QRect const& rect)
{
    if (win::decoration(win)) {
        return rect;
    }
    return win::frame_to_client_rect(win, rect);
}

template<typename Win>
void handle_sync(Win* win)
{
    win->setReadyForPainting();
    win::setup_wayland_plasma_management(win);
    win->sync_request.isPending = false;
    if (win->sync_request.failsafeTimeout) {
        win->sync_request.failsafeTimeout->stop();
    }
    if (win::is_resize(win)) {
        if (win->sync_request.timeout) {
            win->sync_request.timeout->stop();
        }
        win::perform_move_resize(win);
        update_window_pixmap(win);
    } else {
        // setReadyForPainting does as well, but there's a small chance for resize syncs after the
        // resize ended
        win->addRepaintFull();
    }
}

/**
 * Gets the client's normal WM hints and reconfigures itself respectively.
 */
template<typename Win>
void get_wm_normal_hints(Win* win)
{
    auto const hadFixedAspect = win->geometry_hints.hasAspect();

    // roundtrip to X server
    win->geometry_hints.fetch();
    win->geometry_hints.read();

    if (!hadFixedAspect && win->geometry_hints.hasAspect()) {
        // align to eventual new constraints
        win::maximize(win, win->max_mode);
    }

    if (win->m_managed) {
        // update to match restrictions
        auto new_size = win::adjusted_size(win);

        if (new_size != win->size() && !win->control->fullscreen()) {
            auto const orig_client_geo = frame_to_client_rect(win, win->frameGeometry());

            win->resizeWithChecks(new_size);

            if ((!win::is_special_window(win) || win::is_toolbar(win))
                && !win->control->fullscreen()) {
                // try to keep the window in its xinerama screen if possible,
                // if that fails at least keep it visible somewhere
                auto area = workspace()->clientArea(MovementArea, win);
                if (area.contains(orig_client_geo)) {
                    win::keep_in_area(win, area, false);
                }

                area = workspace()->clientArea(WorkArea, win);
                if (area.contains(orig_client_geo)) {
                    win::keep_in_area(win, area, false);
                }
            }
        }
    }

    // affects isResizeable()
    update_allowed_actions(win);
}

template<typename Win>
QSize client_size_base_adjust(Win const* win, QSize const& client_size)
{
    auto const& hints = win->geometry_hints;

    auto const bsize = hints.hasBaseSize() ? hints.baseSize() : hints.minSize();
    auto const increments = hints.resizeIncrements();

    auto increment_grid_align = [](int original_length, int base_length, int increment) {
        // TODO(romangg): This static_cast does absolutely nothing, does it? But then everything
        //                cancels out and this function is redundant.
        auto s = static_cast<int>((original_length - base_length) / increment);
        return s * increment + base_length;
    };

    auto const width = increment_grid_align(client_size.width(), bsize.width(), increments.width());
    auto const height
        = increment_grid_align(client_size.height(), bsize.height(), increments.height());

    return QSize(width, height);
}

template<typename Win>
QSize size_aspect_adjust(Win const* win,
                         QSize const& client_size,
                         QSize const& min_size,
                         QSize const& max_size,
                         win::size_mode mode)
{
    if (!win->geometry_hints.hasAspect()) {
        return client_size;
    }

    // code for aspect ratios based on code from FVWM
    /*
     * The math looks like this:
     *
     * minAspectX    dwidth     maxAspectX
     * ---------- <= ------- <= ----------
     * minAspectY    dheight    maxAspectY
     *
     * If that is multiplied out, then the width and height are
     * invalid in the following situations:
     *
     * minAspectX * dheight > minAspectY * dwidth
     * maxAspectX * dheight < maxAspectY * dwidth
     *
     */

    // use doubles, because the values can be MAX_INT and multiplying would go wrong otherwise
    double const min_aspect_w = win->geometry_hints.minAspect().width();
    double const min_aspect_h = win->geometry_hints.minAspect().height();
    double const max_aspect_w = win->geometry_hints.maxAspect().width();
    double const max_aspect_h = win->geometry_hints.maxAspect().height();

    auto const width_inc = win->geometry_hints.resizeIncrements().width();
    auto const height_inc = win->geometry_hints.resizeIncrements().height();

    // According to ICCCM 4.1.2.3 PMinSize should be a fallback for PBaseSize for size
    // increments, but not for aspect ratio. Since this code comes from FVWM, handles both at
    // the same time, and I have no idea how it works, let's hope nobody relies on that.
    auto const baseSize = win->geometry_hints.baseSize();

    // TODO(romangg): Why?
    auto cl_width = client_size.width() - baseSize.width();
    auto cl_height = client_size.height() - baseSize.height();

    int max_width = max_size.width() - baseSize.width();
    int min_width = min_size.width() - baseSize.width();
    int max_height = max_size.height() - baseSize.height();
    int min_height = min_size.height() - baseSize.height();

    auto aspect_width_grow
        = [min_aspect_w, min_aspect_h, width_inc, max_width](auto& width, auto const& height) {
              if (min_aspect_w * height <= min_aspect_h * width) {
                  // Growth limited by aspect ratio.
                  return;
              }

              auto delta = static_cast<int>((min_aspect_w * height / min_aspect_h - width)
                                            / width_inc * width_inc);
              width = std::min(width + delta, max_width);
          };

    auto aspect_height_grow
        = [max_aspect_w, max_aspect_h, height_inc, max_height](auto const& width, auto& height) {
              if (max_aspect_w * height >= max_aspect_h * width) {
                  // Growth limited by aspect ratio.
                  return;
              }

              auto delta = static_cast<int>((width * max_aspect_h / max_aspect_w - height)
                                            / height_inc * height_inc);
              height = std::min(height + delta, max_height);
          };

    auto aspect_width_grow_height_shrink
        = [min_aspect_w, min_aspect_h, width_inc, height_inc, max_width, min_height](auto& width,
                                                                                     auto& height) {
              if (min_aspect_w * height <= min_aspect_h * width) {
                  // Growth limited by aspect ratio.
                  return;
              }

              auto delta = static_cast<int>(
                  height - width * min_aspect_h / min_aspect_w / height_inc * height_inc);

              if (height - delta >= min_height) {
                  height -= delta;
              } else {
                  auto delta = static_cast<int>((min_aspect_w * height / min_aspect_h - width)
                                                / width_inc * width_inc);
                  width = std::min(width + delta, max_width);
              }
          };

    auto aspect_width_shrink_height_grow
        = [max_aspect_w, max_aspect_h, width_inc, height_inc, min_width, max_height](auto& width,
                                                                                     auto& height) {
              if (max_aspect_w * height >= max_aspect_h * width) {
                  // Growth limited by aspect ratio.
                  return;
              }

              auto delta = static_cast<int>(
                  width - max_aspect_w * height / max_aspect_h / width_inc * width_inc);

              if (width - delta >= min_width) {
                  width -= delta;
              } else {
                  auto delta = static_cast<int>((width * max_aspect_h / max_aspect_w - height)
                                                / height_inc * height_inc);
                  height = std::min(height + delta, max_height);
              }
          };

    switch (mode) {
    case win::size_mode::any:
#if 0
        // make SizeModeAny equal to SizeModeFixedW - prefer keeping fixed width,
        // so that changing aspect ratio to a different value and back keeps the same size (#87298)
        {
            aspect_width_grow_height_shrink(cl_width, cl_height);
            aspect_width_shrink_height_grow(cl_width, cl_height);
            aspect_height_grow(cl_width, cl_height);
            aspect_width_grow(cl_width, cl_height);
            break;
        }
#endif
    case win::size_mode::fixed_width: {
        // the checks are order so that attempts to modify height are first
        aspect_height_grow(cl_width, cl_height);
        aspect_width_grow_height_shrink(cl_width, cl_height);
        aspect_width_shrink_height_grow(cl_width, cl_height);
        aspect_width_grow(cl_width, cl_height);
        break;
    }
    case win::size_mode::fixed_height: {
        aspect_width_grow(cl_width, cl_height);
        aspect_width_shrink_height_grow(cl_width, cl_height);
        aspect_width_grow_height_shrink(cl_width, cl_height);
        aspect_height_grow(cl_width, cl_height);
        break;
    }
    case win::size_mode::max: {
        // first checks that try to shrink
        aspect_width_grow_height_shrink(cl_width, cl_height);
        aspect_width_shrink_height_grow(cl_width, cl_height);
        aspect_width_grow(cl_width, cl_height);
        aspect_height_grow(cl_width, cl_height);
        break;
    }
    }

    cl_width += baseSize.width();
    cl_height += baseSize.height();

    return QSize(cl_width, cl_height);
}

/**
 * Calculate the appropriate frame size for the given client size @arg client_size.
 *
 * @arg client_size is adapted according to the window's size hints (minimum, maximum and
 * incremental size changes).
 */
template<typename Win>
QSize size_for_client_size(Win const* win,
                           QSize const& client_size,
                           win::size_mode mode,
                           bool noframe)
{
    auto cl_width = client_size.width();
    auto cl_height = client_size.height();

    if (cl_width < 1 || cl_height < 1) {
        qCWarning(KWIN_CORE) << "size_for_client_size(..) with empty size!";

        if (cl_width < 1) {
            cl_width = 1;
        }
        if (cl_height < 1) {
            cl_height = 1;
        }
    }

    // basesize, minsize, maxsize, paspect and resizeinc have all values defined,
    // even if they're not set in flags - see getWmNormalHints()
    auto min_size = win->minSize();
    auto max_size = win->maxSize();

    // TODO(romangg): Remove?
    if (win::decoration(win)) {
        auto deco_size = frame_size(win);

        min_size.setWidth(std::max(deco_size.width(), min_size.width()));
        min_size.setHeight(std::max(deco_size.height(), min_size.height()));
    }

    cl_width = std::min(max_size.width(), cl_width);
    cl_height = std::min(max_size.height(), cl_height);

    cl_width = std::max(min_size.width(), cl_width);
    cl_height = std::max(min_size.height(), cl_height);

    auto size = QSize(cl_width, cl_height);

    if (win->control->rules().checkStrictGeometry(!win->control->fullscreen())) {
        auto const base_adjusted_size = client_size_base_adjust(win, size);
        size = size_aspect_adjust(win, base_adjusted_size, min_size, max_size, mode);
    }

    if (!noframe) {
        size = client_to_frame_size(win, size);
    }

    return win->control->rules().checkSize(size);
}

template<typename Win>
QPoint gravity_adjustment(Win* win, xcb_gravity_t gravity)
{
    int dx = 0;
    int dy = 0;

    // dx, dy specify how the client window moves to make space for the frame.
    // In general we have to compute the reference point and from that figure
    // out how much we need to shift the client, however given that we ignore
    // the border width attribute and the extents of the server-side decoration
    // are known in advance, we can simplify the math quite a bit and express
    // the required window gravity adjustment in terms of border sizes.
    switch (gravity) {
    case XCB_GRAVITY_NORTH_WEST:
        // move down right
    default:
        dx = win::left_border(win);
        dy = win::top_border(win);
        break;
    case XCB_GRAVITY_NORTH:
        // move right
        dx = 0;
        dy = win::top_border(win);
        break;
    case XCB_GRAVITY_NORTH_EAST:
        // move down left
        dx = -win::right_border(win);
        dy = win::top_border(win);
        break;
    case XCB_GRAVITY_WEST:
        // move right
        dx = win::left_border(win);
        dy = 0;
        break;
    case XCB_GRAVITY_CENTER:
        dx = (win::left_border(win) - win::right_border(win)) / 2;
        dy = (win::top_border(win) - win::bottom_border(win)) / 2;
        break;
    case XCB_GRAVITY_STATIC:
        // don't move
        dx = 0;
        dy = 0;
        break;
    case XCB_GRAVITY_EAST:
        // move left
        dx = -win::right_border(win);
        dy = 0;
        break;
    case XCB_GRAVITY_SOUTH_WEST:
        // move up right
        dx = win::left_border(win);
        dy = -win::bottom_border(win);
        break;
    case XCB_GRAVITY_SOUTH:
        // move up
        dx = 0;
        dy = -win::bottom_border(win);
        break;
    case XCB_GRAVITY_SOUTH_EAST:
        // move up left
        dx = -win::right_border(win);
        dy = -win::bottom_border(win);
        break;
    }

    return QPoint(dx, dy);
}

template<typename Win>
QPoint calculate_gravitation(Win* win, bool invert)
{
    auto const adjustment = gravity_adjustment(win, win->geometry_hints.windowGravity());

    // translate from client movement to frame movement
    auto const dx = adjustment.x() - win::left_border(win);
    auto const dy = adjustment.y() - win::top_border(win);

    if (invert) {
        return QPoint(win->pos().x() - dx, win->pos().y() - dy);
    }
    return QPoint(win->pos().x() + dx, win->pos().y() + dy);
}

template<typename Win>
bool configure_should_ignore(Win* win, int& value_mask)
{
    auto const position_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    auto const size_mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    auto const geometry_mask = position_mask | size_mask;

    // we want to (partially) ignore the request when the window is somehow maximized or quicktiled
    auto ignore = !win->app_no_border
        && (win->control->quicktiling() != win::quicktiles::none
            || win->maximizeMode() != win::maximize_mode::restore);

    // however, the user shall be able to force obedience despite and also disobedience in general
    ignore = win->control->rules().checkIgnoreGeometry(ignore);

    if (!ignore) {
        // either we're not max'd / q'tiled or the user allowed the client to break that - so break
        // it.
        win->control->set_quicktiling(win::quicktiles::none);
        win->max_mode = win::maximize_mode::restore;
        Q_EMIT win->quicktiling_changed();
    } else if (!win->app_no_border && win->control->quicktiling() == win::quicktiles::none
               && (win->maximizeMode() == win::maximize_mode::vertical
                   || win->maximizeMode() == win::maximize_mode::horizontal)) {
        // ignoring can be, because either we do, or the user does explicitly not want it.
        // for partially maximized windows we want to allow configures in the other dimension.
        // so we've to ask the user again - to know whether we just ignored for the partial
        // maximization. the problem here is, that the user can explicitly permit configure requests
        // - even for maximized windows! we cannot distinguish that from passing "false" for
        // partially maximized windows.
        ignore = win->control->rules().checkIgnoreGeometry(false);

        if (!ignore) {
            // the user is not interested, so we fix up dimensions
            if (win->maximizeMode() == win::maximize_mode::vertical) {
                value_mask &= ~(XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT);
            }
            if (win->maximizeMode() == win::maximize_mode::horizontal) {
                value_mask &= ~(XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_WIDTH);
            }
            if (!(value_mask & geometry_mask)) {
                // the modification turned the request void
                ignore = true;
            }
        }
    }

    return ignore;
}

template<typename Win>
void configure_position_size_from_request(Win* win,
                                          QRect const& requested_geo,
                                          int& value_mask,
                                          int gravity,
                                          bool from_tool)
{
    // We calculate in client coordinates.
    auto client_pos = frame_to_client_pos(win, win->pos());
    client_pos -= gravity_adjustment(win, xcb_gravity_t(gravity));

    if (value_mask & XCB_CONFIG_WINDOW_X) {
        client_pos.setX(requested_geo.x());
    }
    if (value_mask & XCB_CONFIG_WINDOW_Y) {
        client_pos.setY(requested_geo.y());
    }

    auto orig_client_geo = frame_to_client_rect(win, win->frameGeometry());

    // clever(?) workaround for applications like xv that want to set
    // the location to the current location but miscalculate the
    // frame size due to kwin being a double-reparenting window
    // manager
    if (client_pos.x() == orig_client_geo.x() && client_pos.y() == orig_client_geo.y()
        && gravity == XCB_GRAVITY_NORTH_WEST && !from_tool) {
        client_pos.setX(win->pos().x());
        client_pos.setY(win->pos().y());
    }

    client_pos += gravity_adjustment(win, xcb_gravity_t(gravity));
    client_pos = client_to_frame_pos(win, client_pos);

    auto client_size = frame_to_client_size(win, win->size());

    if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
        client_size.setWidth(requested_geo.width());
    }
    if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
        client_size.setHeight(requested_geo.height());
    }

    // enforces size if needed
    auto ns = win->sizeForClientSize(client_size);
    client_pos = win->control->rules().checkPosition(client_pos);
    int newScreen = screens()->number(QRect(client_pos, ns).center());

    if (newScreen != win->control->rules().checkScreen(newScreen)) {
        // not allowed by rule
        return;
    }

    geometry_updates_blocker blocker(win);
    win::move(win, client_pos);
    plain_resize(win, ns);

    auto area = workspace()->clientArea(WorkArea, win);

    if (!from_tool && (!win::is_special_window(win) || win::is_toolbar(win))
        && !win->control->fullscreen() && area.contains(orig_client_geo)) {
        win::keep_in_area(win, area, false);
    }

    // this is part of the kicker-xinerama-hack... it should be
    // safe to remove when kicker gets proper ExtendedStrut support;
    // see Workspace::updateClientArea() and
    // X11Client::adjustedClientArea()
    if (win->hasStrut()) {
        workspace()->updateClientArea();
    }
}

template<typename Win>
void configure_only_size_from_request(Win* win,
                                      QRect const& requested_geo,
                                      int& value_mask,
                                      int gravity,
                                      bool from_tool)
{
    // pure resize
    auto const client_size = frame_to_client_size(win, win->size());
    int nw = client_size.width();
    int nh = client_size.height();

    if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
        nw = requested_geo.width();
    }
    if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
        nh = requested_geo.height();
    }

    auto ns = win->sizeForClientSize(QSize(nw, nh));

    if (ns != win->size()) {
        // don't restore if some app sets its own size again
        auto orig_client_geo = frame_to_client_rect(win, win->frameGeometry());
        win::geometry_updates_blocker blocker(win);
        resize_with_checks(win, ns, xcb_gravity_t(gravity));

        if (!from_tool && (!win::is_special_window(win) || win::is_toolbar(win))
            && !win->control->fullscreen()) {
            // try to keep the window in its xinerama screen if possible,
            // if that fails at least keep it visible somewhere

            auto area = workspace()->clientArea(MovementArea, win);
            if (area.contains(orig_client_geo)) {
                win::keep_in_area(win, area, false);
            }

            area = workspace()->clientArea(WorkArea, win);
            if (area.contains(orig_client_geo)) {
                win::keep_in_area(win, area, false);
            }
        }
    }
}

template<typename Win>
void configure_request(Win* win,
                       int value_mask,
                       int rx,
                       int ry,
                       int rw,
                       int rh,
                       int gravity,
                       bool from_tool)
{
    auto const requested_geo = QRect(rx, ry, rw, rh);
    auto const position_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    auto const size_mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    auto const geometry_mask = position_mask | size_mask;

    // "maximized" is a user setting -> we do not allow the client to resize itself
    // away from this & against the users explicit wish
    qCDebug(KWIN_CORE) << win << bool(value_mask & geometry_mask)
                       << bool(win->maximizeMode() & win::maximize_mode::vertical)
                       << bool(win->maximizeMode() & win::maximize_mode::horizontal);

    if (configure_should_ignore(win, value_mask)) {
        // nothing (left) to do for use - bugs #158974, #252314, #321491
        qCDebug(KWIN_CORE) << "DENIED";
        return;
    }

    qCDebug(KWIN_CORE) << "PERMITTED" << win << bool(value_mask & geometry_mask);

    if (gravity == 0) {
        // default (nonsense) value for the argument
        gravity = win->geometry_hints.windowGravity();
    }

    if (value_mask & position_mask) {
        configure_position_size_from_request(win, requested_geo, value_mask, gravity, from_tool);
    }

    if (value_mask & size_mask && !(value_mask & position_mask)) {
        configure_only_size_from_request(win, requested_geo, value_mask, gravity, from_tool);
    }

    win->restore_geometries.maximize = win->frameGeometry();

    // No need to send synthetic configure notify event here, either it's sent together
    // with geometry change, or there's no need to send it.
    // Handling of the real ConfigureRequest event forces sending it, as there it's necessary.
}

template<typename Win>
void resize_with_checks(Win* win,
                        QSize const& size,
                        xcb_gravity_t gravity,
                        win::force_geometry force = win::force_geometry::no)
{
    auto width = size.width();
    auto height = size.height();

    if (win::shaded(win)) {
        if (height == win::top_border(win) + win::bottom_border(win)) {
            qCWarning(KWIN_CORE) << "Shaded geometry passed for size:";
        }
    }

    auto pos_x = win->pos().x();
    auto pos_y = win->pos().y();

    auto area = workspace()->clientArea(WorkArea, win);

    // Don't allow growing larger than workarea.
    if (width > area.width()) {
        width = area.width();
    }
    if (height > area.height()) {
        height = area.height();
    }

    // checks size constraints, including min/max size
    auto const tmp_size = win::adjusted_size(win, QSize(width, height), win::size_mode::any);
    width = tmp_size.width();
    height = tmp_size.height();

    if (gravity == 0) {
        gravity = win->geometry_hints.windowGravity();
    }

    switch (gravity) {
    case XCB_GRAVITY_NORTH_WEST:
        // top left corner doesn't move
    default:
        break;
    case XCB_GRAVITY_NORTH:
        // middle of top border doesn't move
        pos_x = (pos_x + win->size().width() / 2) - (width / 2);
        break;
    case XCB_GRAVITY_NORTH_EAST:
        // top right corner doesn't move
        pos_x = pos_x + win->size().width() - width;
        break;
    case XCB_GRAVITY_WEST:
        // middle of left border doesn't move
        pos_y = (pos_y + win->size().height() / 2) - (height / 2);
        break;
    case XCB_GRAVITY_CENTER:
        // middle point doesn't move
        pos_x = (pos_x + win->size().width() / 2) - (width / 2);
        pos_y = (pos_y + win->size().height() / 2) - (height / 2);
        break;
    case XCB_GRAVITY_STATIC:
        // top left corner of _client_ window doesn't move
        // since decoration doesn't change, equal to NorthWestGravity
        break;
    case XCB_GRAVITY_EAST:
        // middle of right border doesn't move
        pos_x = pos_x + win->size().width() - width;
        pos_y = (pos_y + win->size().height() / 2) - (height / 2);
        break;
    case XCB_GRAVITY_SOUTH_WEST:
        // bottom left corner doesn't move
        pos_y = pos_y + win->size().height() - height;
        break;
    case XCB_GRAVITY_SOUTH:
        // middle of bottom border doesn't move
        pos_x = (pos_x + win->size().width() / 2) - (width / 2);
        pos_y = pos_y + win->size().height() - height;
        break;
    case XCB_GRAVITY_SOUTH_EAST:
        // bottom right corner doesn't move
        pos_x = pos_x + win->size().width() - width;
        pos_y = pos_y + win->size().height() - height;
        break;
    }

    win->setFrameGeometry(QRect(pos_x, pos_y, width, height), force);
}

/**
 * Implements _NET_MOVERESIZE_WINDOW.
 */
template<typename Win>
void net_move_resize_window(Win* win, int flags, int x, int y, int width, int height)
{
    int gravity = flags & 0xff;
    int value_mask = 0;

    if (flags & (1 << 8)) {
        value_mask |= XCB_CONFIG_WINDOW_X;
    }
    if (flags & (1 << 9)) {
        value_mask |= XCB_CONFIG_WINDOW_Y;
    }
    if (flags & (1 << 10)) {
        value_mask |= XCB_CONFIG_WINDOW_WIDTH;
    }
    if (flags & (1 << 11)) {
        value_mask |= XCB_CONFIG_WINDOW_HEIGHT;
    }

    configure_request(win, value_mask, x, y, width, height, gravity, true);
}

template<typename Win>
void plain_resize(Win* win, int w, int h, win::force_geometry force = win::force_geometry::no)
{
    QSize frameSize(w, h);
    QSize bufferSize;

    if (win::shaded(win)) {
        frameSize.setHeight(win::top_border(win) + win::bottom_border(win));
    }

    if (win::decoration(win)) {
        bufferSize = frameSize;
    } else {
        bufferSize = frame_to_client_rect(win, win->frameGeometry()).size();
    }
    if (!win->control->geometry_update.block
        && frameSize != win->control->rules().checkSize(frameSize)) {
        qCDebug(KWIN_CORE) << "forced size fail:" << frameSize << ":"
                           << win->control->rules().checkSize(frameSize);
    }

    auto const old_buffer_geo = win->bufferGeometry();
    win->set_frame_geometry(QRect(win->frameGeometry().topLeft(), frameSize));

    // resuming geometry updates is handled only in setGeometry()
    assert(win->control->geometry_update.pending == win::pending_geometry::none
           || win->control->geometry_update.block);

    if (force == win::force_geometry::no && old_buffer_geo.size() == win->bufferGeometry().size()) {
        return;
    }

    if (win->control->geometry_update.block) {
        if (win->control->geometry_update.pending == win::pending_geometry::forced) {
            // maximum, nothing needed
        } else if (force == win::force_geometry::yes) {
            win->control->geometry_update.pending = win::pending_geometry::forced;
        } else {
            win->control->geometry_update.pending = win::pending_geometry::normal;
        }
        return;
    }

    update_server_geometry(win);
    win->updateWindowRules(static_cast<Rules::Types>(Rules::Position | Rules::Size));
    screens()->setCurrent(win);
    workspace()->updateStackingOrder();

    if (win->control->geometry_update.original.buffer.size() != win->bufferGeometry().size()) {
        win->discardWindowPixmap();
    }

    Q_EMIT win->geometryShapeChanged(win, win->control->geometry_update.original.frame);
    win::add_repaint_during_geometry_updates(win);
    win->control->update_geometry_before_update_blocking();

    // TODO: this signal is emitted too often
    Q_EMIT win->geometryChanged();
}

template<typename Win>
void plain_resize(Win* win, QSize const& size, win::force_geometry force = win::force_geometry::no)
{
    plain_resize(win, size.width(), size.height(), force);
}

template<typename Win>
void update_server_geometry(Win* win)
{
    auto const oldBufferGeometry = win->control->geometry_update.original.buffer;

    if (oldBufferGeometry.size() != win->bufferGeometry().size()
        || win->control->geometry_update.pending == win::pending_geometry::forced) {
        // Resizes the decoration, and makes sure the decoration widget gets resize event
        // even if the size hasn't changed. This is needed to make sure the decoration
        // re-layouts (e.g. when maximization state changes,
        // the decoration may alter some borders, but the actual size
        // of the decoration stays the same).
        win::trigger_decoration_repaint(win);
        update_input_window(win);

        // If the client is being interactively resized, then the frame window, the wrapper window,
        // and the client window have correct geometry at this point, so we don't have to configure
        // them again. If the client doesn't support frame counters, always update geometry.
        auto const needsGeometryUpdate
            = !win::is_resize(win) || win->sync_request.counter == XCB_NONE;

        if (needsGeometryUpdate) {
            win->xcb_windows.outer.setGeometry(win->bufferGeometry());
        }

        if (!win::shaded(win)) {
            if (needsGeometryUpdate) {
                win->xcb_windows.wrapper.setGeometry(frame_relative_client_rect(win));
                win->xcb_windows.client.resize(frame_to_client_size(win, win->size()));
            }
            // SELI - won't this be too expensive?
            // THOMAS - yes, but gtk+ clients will not resize without ...
            send_synthetic_configure_notify(win);
        }

        update_shape(win);
    } else {
        if (win->control->move_resize().enabled) {
            if (win::compositing()) {
                // Defer the X update until we leave this mode
                win->needs_x_move = true;
            } else {
                // sendSyntheticConfigureNotify() on finish shall be sufficient
                win->xcb_windows.outer.move(win->bufferGeometry().topLeft());
            }
        } else {
            win->xcb_windows.outer.move(win->bufferGeometry().topLeft());
            send_synthetic_configure_notify(win);
        }

        // Unconditionally move the input window: it won't affect rendering
        win->xcb_windows.input.move(win->pos() + win->input_offset);
    }
}

template<typename Win>
void reposition_geometry_tip(Win* win)
{
    assert(is_move(win) || is_resize(win));

    // Position and Size display
    if (effects && static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::GeometryTip)) {
        // some effect paints this for us
        return;
    }
    if (!options->showGeometryTip()) {
        return;
    }

    if (!win->geometry_tip) {
        win->geometry_tip = new GeometryTip(&win->geometry_hints);
    }

    // Position of the frame, size of the window itself.
    auto geo = win->control->move_resize().geometry;
    auto const frame_size = win->size();
    auto const client_size = frame_to_client_size(win, win->size());

    geo.setWidth(geo.width() - (frame_size.width() - client_size.width()));
    geo.setHeight(geo.height() - (frame_size.height() - client_size.height()));

    if (shaded(win)) {
        geo.setHeight(0);
    }

    win->geometry_tip->setGeometry(geo);
    if (!win->geometry_tip->isVisible()) {
        win->geometry_tip->show();
    }
    win->geometry_tip->raise();
}

/**
 * Calculates the bounding rectangle defined by the 4 monitor indices indicating the
 * top, bottom, left, and right edges of the window when the fullscreen state is enabled.
 */
inline QRect fullscreen_monitors_area(NETFullscreenMonitors requestedTopology)
{
    QRect top, bottom, left, right, total;

    top = screens()->geometry(requestedTopology.top);
    bottom = screens()->geometry(requestedTopology.bottom);
    left = screens()->geometry(requestedTopology.left);
    right = screens()->geometry(requestedTopology.right);
    total = top.united(bottom.united(left.united(right)));

    return total;
}

template<typename Win>
void update_fullscreen_monitors(Win* win, NETFullscreenMonitors topology)
{
    auto count = screens()->count();

    if (topology.top >= count || topology.bottom >= count || topology.left >= count
        || topology.right >= count) {
        qCWarning(KWIN_CORE)
            << "fullscreenMonitors update failed. request higher than number of screens.";
        return;
    }

    win->info->setFullscreenMonitors(topology);
    if (win->control->fullscreen()) {
        win->setFrameGeometry(fullscreen_monitors_area(topology));
    }
}

template<typename Win>
NETExtendedStrut strut(Win const* win)
{
    NETExtendedStrut ext = win->info->extendedStrut();
    NETStrut str = win->info->strut();
    auto const displaySize = screens()->displaySize();

    if (ext.left_width == 0 && ext.right_width == 0 && ext.top_width == 0 && ext.bottom_width == 0
        && (str.left != 0 || str.right != 0 || str.top != 0 || str.bottom != 0)) {
        // build extended from simple
        if (str.left != 0) {
            ext.left_width = str.left;
            ext.left_start = 0;
            ext.left_end = displaySize.height();
        }
        if (str.right != 0) {
            ext.right_width = str.right;
            ext.right_start = 0;
            ext.right_end = displaySize.height();
        }
        if (str.top != 0) {
            ext.top_width = str.top;
            ext.top_start = 0;
            ext.top_end = displaySize.width();
        }
        if (str.bottom != 0) {
            ext.bottom_width = str.bottom;
            ext.bottom_start = 0;
            ext.bottom_end = displaySize.width();
        }
    }
    return ext;
}

template<typename Win>
QRect adjusted_client_area(Win const* win, QRect const& desktopArea, QRect const& area)
{
    auto rect = area;
    NETExtendedStrut str = strut(win);

    QRect stareaL = QRect(0, str.left_start, str.left_width, str.left_end - str.left_start + 1);
    QRect stareaR = QRect(desktopArea.right() - str.right_width + 1,
                          str.right_start,
                          str.right_width,
                          str.right_end - str.right_start + 1);
    QRect stareaT = QRect(str.top_start, 0, str.top_end - str.top_start + 1, str.top_width);
    QRect stareaB = QRect(str.bottom_start,
                          desktopArea.bottom() - str.bottom_width + 1,
                          str.bottom_end - str.bottom_start + 1,
                          str.bottom_width);

    auto screenarea = workspace()->clientArea(ScreenArea, win);

    // HACK: workarea handling is not xinerama aware, so if this strut
    // reserves place at a xinerama edge that's inside the virtual screen,
    // ignore the strut for workspace setting.
    if (area == QRect(QPoint(0, 0), screens()->displaySize())) {
        if (stareaL.left() < screenarea.left())
            stareaL = QRect();
        if (stareaR.right() > screenarea.right())
            stareaR = QRect();
        if (stareaT.top() < screenarea.top())
            stareaT = QRect();
        if (stareaB.bottom() < screenarea.bottom())
            stareaB = QRect();
    }

    // Handle struts at xinerama edges that are inside the virtual screen.
    // They're given in virtual screen coordinates, make them affect only
    // their xinerama screen.
    stareaL.setLeft(qMax(stareaL.left(), screenarea.left()));
    stareaR.setRight(qMin(stareaR.right(), screenarea.right()));
    stareaT.setTop(qMax(stareaT.top(), screenarea.top()));
    stareaB.setBottom(qMin(stareaB.bottom(), screenarea.bottom()));

    if (stareaL.intersects(area)) {
        rect.setLeft(stareaL.right() + 1);
    }
    if (stareaR.intersects(area)) {
        rect.setRight(stareaR.left() - 1);
    }
    if (stareaT.intersects(area)) {
        rect.setTop(stareaT.bottom() + 1);
    }
    if (stareaB.intersects(area)) {
        rect.setBottom(stareaB.top() - 1);
    }

    return rect;
}

template<typename Win>
StrutRect strut_rect(Win const* win, StrutArea area)
{
    // Not valid
    assert(area != StrutAreaAll);

    auto const displaySize = screens()->displaySize();
    NETExtendedStrut strutArea = strut(win);

    switch (area) {
    case StrutAreaTop:
        if (strutArea.top_width != 0)
            return StrutRect(QRect(strutArea.top_start,
                                   0,
                                   strutArea.top_end - strutArea.top_start,
                                   strutArea.top_width),
                             StrutAreaTop);
        break;
    case StrutAreaRight:
        if (strutArea.right_width != 0)
            return StrutRect(QRect(displaySize.width() - strutArea.right_width,
                                   strutArea.right_start,
                                   strutArea.right_width,
                                   strutArea.right_end - strutArea.right_start),
                             StrutAreaRight);
        break;
    case StrutAreaBottom:
        if (strutArea.bottom_width != 0)
            return StrutRect(QRect(strutArea.bottom_start,
                                   displaySize.height() - strutArea.bottom_width,
                                   strutArea.bottom_end - strutArea.bottom_start,
                                   strutArea.bottom_width),
                             StrutAreaBottom);
        break;
    case StrutAreaLeft:
        if (strutArea.left_width != 0)
            return StrutRect(QRect(0,
                                   strutArea.left_start,
                                   strutArea.left_width,
                                   strutArea.left_end - strutArea.left_start),
                             StrutAreaLeft);
        break;
    default:
        // Not valid
        abort();
    }

    return StrutRect();
}

template<typename Win>
StrutRects strut_rects(Win const* win)
{
    StrutRects region;
    region += strut_rect(win, StrutAreaTop);
    region += strut_rect(win, StrutAreaRight);
    region += strut_rect(win, StrutAreaBottom);
    region += strut_rect(win, StrutAreaLeft);
    return region;
}

template<typename Win>
bool has_offscreen_xinerama_strut(Win const* win)
{
    // Get strut as a QRegion
    QRegion region;
    region += strut_rect(win, StrutAreaTop);
    region += strut_rect(win, StrutAreaRight);
    region += strut_rect(win, StrutAreaBottom);
    region += strut_rect(win, StrutAreaLeft);

    // Remove all visible areas so that only the invisible remain
    for (int i = 0; i < screens()->count(); i++) {
        region -= screens()->geometry(i);
    }

    // If there's anything left then we have an offscreen strut
    return !region.isEmpty();
}

}
