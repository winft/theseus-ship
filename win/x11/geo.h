/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "scene.h"

#include "win/setup.h"

#include <xcb/sync.h>
#include <xcb/xcb_icccm.h>

namespace KWin::win::x11
{

template<typename Win>
class sync_suppressor
{
public:
    explicit sync_suppressor(Win* window)
        : window{window}
    {
        window->sync_request.suppressed++;
    }
    ~sync_suppressor()
    {
        window->sync_request.suppressed--;
    }

private:
    Win* window;
};

template<typename Win>
void update_shape(Win* win)
{
    if (win->is_shape) {
        // Workaround for #19644 - Shaped windows shouldn't have decoration
        if (!win->app_no_border) {
            // Only when shape is detected for the first time, still let the user to override
            win->app_no_border = true;
            win->user_no_border = win->control->rules.checkNoBorder(true);
            win->updateDecoration(true);
        }
        if (win->noBorder()) {
            auto const client_pos = QPoint(left_border(win), top_border(win));
            xcb_shape_combine(connection(),
                              XCB_SHAPE_SO_SET,
                              XCB_SHAPE_SK_BOUNDING,
                              XCB_SHAPE_SK_BOUNDING,
                              win->frameId(),
                              client_pos.x(),
                              client_pos.y(),
                              win->xcb_window);
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
        win->user_no_border = win->control->rules.checkNoBorder(win->user_no_border
                                                                || win->motif_hints.no_border());
        win->updateDecoration(true);
    }

    // Decoration mask (i.e. 'else' here) setting is done in setMask()
    // when the decoration calls it or when the decoration is created/destroyed
    win->update_input_shape();

    if (win->render) {
        win->addRepaintFull();

        // In case shape change removes part of this window
        win->space.base.render->compositor->addRepaint(visible_rect(win));
    }

    win->discard_shape();
}

template<typename Win>
void apply_pending_geometry(Win* win, int64_t update_request_number)
{
    if (win->pending_configures.empty()) {
        // Can happen when we did a sync-suppressed update in-between or when a client is rogue.
        return;
    }

    auto frame_geo = win->frameGeometry();
    auto max_mode = win->max_mode;
    auto fullscreen = win->control->fullscreen;

    for (auto it = win->pending_configures.begin(); it != win->pending_configures.end(); it++) {
        if (it->update_request_number > update_request_number) {
            // TODO(romangg): Remove?
            win->synced_geometry.client = it->geometry.client;
            return;
        }
        if (it->update_request_number == update_request_number) {
            frame_geo = it->geometry.frame;
            max_mode = it->geometry.max_mode;
            fullscreen = it->geometry.fullscreen;

            // Removes all previous pending configures including this one.
            win->pending_configures.erase(win->pending_configures.begin(), ++it);
            break;
        }
    }

    auto resizing = is_resize(win);

    if (resizing) {
        // Adjust the geometry according to the resize process.
        // We must adjust frame geometry because configure events carry the maximum window geometry
        // size. A client with aspect ratio can attach a buffer with smaller size than the one in
        // a configure event.
        auto& mov_res = win->control->move_resize;

        switch (mov_res.contact) {
        case position::top_left:
            frame_geo.moveRight(mov_res.geometry.right());
            frame_geo.moveBottom(mov_res.geometry.bottom());
            break;
        case position::top:
        case position::top_right:
            frame_geo.moveLeft(mov_res.geometry.left());
            frame_geo.moveBottom(mov_res.geometry.bottom());
            break;
        case position::right:
        case position::bottom_right:
        case position::bottom:
            frame_geo.moveLeft(mov_res.geometry.left());
            frame_geo.moveTop(mov_res.geometry.top());
            break;
        case position::bottom_left:
        case position::left:
            frame_geo.moveRight(mov_res.geometry.right());
            frame_geo.moveTop(mov_res.geometry.top());
            break;
        case position::center:
            Q_UNREACHABLE();
        }
    }

    win->do_set_fullscreen(fullscreen);
    win->do_set_geometry(frame_geo);
    win->do_set_maximize_mode(max_mode);

    update_window_buffer(win);

    if (resizing) {
        update_move_resize(win, win->space.input->platform.cursor->pos());
    }
}

template<typename Win>
bool needs_sync(Win* win)
{
    if (!win->sync_request.counter) {
        return false;
    }

    auto const& update = win->geometry_update;

    if (update.max_mode != win->synced_geometry.max_mode) {
        return true;
    }
    if (update.fullscreen != win->synced_geometry.fullscreen) {
        return true;
    }

    auto ref_geo = update.client;
    if (ref_geo.isEmpty()) {
        ref_geo = QRect();
    }

    return ref_geo.size().isEmpty() || ref_geo.size() != win->synced_geometry.client.size();
}

template<typename Win>
void handle_sync(Win* win, xcb_sync_int64_t counter_value)
{
    auto update_request_number = static_cast<int64_t>(counter_value.hi);
    update_request_number = update_request_number << 32;
    update_request_number += counter_value.lo;

    if (update_request_number == 0) {
        // The alarm triggers initially on 0. Ignore that one.
        return;
    }

    win->setReadyForPainting();

    apply_pending_geometry(win, update_request_number);
}

/**
 * Gets the client's normal WM hints and reconfigures itself respectively.
 */
template<typename Win>
void get_wm_normal_hints(Win* win)
{
    auto const hadFixedAspect = win->geometry_hints.has_aspect();

    // roundtrip to X server
    win->geometry_hints.fetch();
    win->geometry_hints.read();

    if (!hadFixedAspect && win->geometry_hints.has_aspect()) {
        // align to eventual new constraints
        win::maximize(win, win->max_mode);
    }

    if (win->control) {
        // update to match restrictions
        // TODO(romangg): adjust to restrictions.
        auto new_size = win->frameGeometry().size();

        if (new_size != win->size() && !win->control->fullscreen) {
            auto const orig_client_geo = frame_to_client_rect(win, win->frameGeometry());

            constrained_resize(win, new_size);

            if ((!win::is_special_window(win) || win::is_toolbar(win))
                && !win->control->fullscreen) {
                // try to keep the window in its xinerama screen if possible,
                // if that fails at least keep it visible somewhere
                auto area = space_window_area(win->space, MovementArea, win);
                if (area.contains(orig_client_geo)) {
                    win::keep_in_area(win, area, false);
                }

                area = space_window_area(win->space, WorkArea, win);
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

    auto const bsize = hints.has_base_size() ? hints.base_size() : hints.min_size();
    auto const increments = hints.resize_increments();

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
    if (!win->geometry_hints.has_aspect()) {
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
    double const min_aspect_w = win->geometry_hints.min_aspect().width();
    double const min_aspect_h = win->geometry_hints.min_aspect().height();
    double const max_aspect_w = win->geometry_hints.max_aspect().width();
    double const max_aspect_h = win->geometry_hints.max_aspect().height();

    auto const width_inc = win->geometry_hints.resize_increments().width();
    auto const height_inc = win->geometry_hints.resize_increments().height();

    // According to ICCCM 4.1.2.3 PMinSize should be a fallback for PBaseSize for size
    // increments, but not for aspect ratio. Since this code comes from FVWM, handles both at
    // the same time, and I have no idea how it works, let's hope nobody relies on that.
    auto const baseSize = win->geometry_hints.base_size();

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
    auto cl_width = std::max(1, client_size.width());
    auto cl_height = std::max(1, client_size.height());

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

    if (win->control->rules.checkStrictGeometry(!win->control->fullscreen)) {
        auto const base_adjusted_size = client_size_base_adjust(win, size);
        size = size_aspect_adjust(win, base_adjusted_size, min_size, max_size, mode);
    }

    if (!noframe) {
        size = client_to_frame_size(win, size);
    }

    return win->control->rules.checkSize(size);
}

template<typename Win>
inline QMargins gtk_frame_extents(Win* win)
{
    auto const strut = win->info->gtkFrameExtents();
    return QMargins(strut.left, strut.top, strut.right, strut.bottom);
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
    auto const adjustment = gravity_adjustment(win, win->geometry_hints.window_gravity());

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
    // When app allows deco then (partially) ignore request when (semi-)maximized or quicktiled.
    auto const quicktiled = win->control->quicktiling != quicktiles::none;
    auto const maximized = win->maximizeMode() != maximize_mode::restore;

    auto ignore = !win->app_no_border && (quicktiled || maximized);

    if (!win->control->rules.checkIgnoreGeometry(ignore)) {
        // Not maximized, quicktiled or the user allowed the client to break it via rule.
        win->control->quicktiling = win::quicktiles::none;
        win->max_mode = win::maximize_mode::restore;
        if (quicktiled || maximized) {
            // TODO(romangg): not emit on maximized?
            Q_EMIT win->qobject->quicktiling_changed();
        }
        return false;
    }

    if (is_on_screen_display(win)) {
        // Only we set the position of OSDs.
        // TODO(romangg): That fixes a regression in Plasma Workspace [1] where the position of the
        //                OSD is configured to (0,0). It would be better to fix Plasma.
        //                [1] https://invent.kde.org/plasma/plasma-workspace/-/commit/e4ea7286.
        return true;
    }

    if (win->app_no_border) {
        // Without borders do not ignore.
        return false;
    }

    if (quicktiled) {
        // Configure should be ignored when quicktiled.
        return true;
    }

    if (win->maximizeMode() == maximize_mode::full) {
        // When maximized fully ignore the request.
        return true;
    }

    if (win->maximizeMode() == maximize_mode::restore) {
        // Common case of a window that is not maximized where we allow the configure.
        return false;
    }

    // Special case with a partially maximized window. Here allow configure requests in the
    // direction that is not maximized.
    //
    // First ask again the user if he wants to ignore such requests.
    if (win->control->rules.checkIgnoreGeometry(false)) {
        return true;
    }

    // Remove the flags to only allow the partial configure request.
    if (win->maximizeMode() == win::maximize_mode::vertical) {
        value_mask &= ~(XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT);
    }
    if (win->maximizeMode() == win::maximize_mode::horizontal) {
        value_mask &= ~(XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_WIDTH);
    }

    auto const position_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    auto const size_mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    auto const geometry_mask = position_mask | size_mask;

    auto const configure_does_geometry_change = value_mask & geometry_mask;

    // We ignore when there is no geometry change remaining anymore.
    return !configure_does_geometry_change;
}

template<typename Win>
void configure_position_size_from_request(Win* win,
                                          QRect const& requested_geo,
                                          int& value_mask,
                                          int gravity,
                                          bool from_tool)
{
    // We calculate in client coordinates.
    auto const orig_client_geo = win->synced_geometry.client;
    auto client_size = orig_client_geo.size();

    auto client_pos = orig_client_geo.topLeft();
    client_pos -= gravity_adjustment(win, xcb_gravity_t(gravity));

    if (value_mask & XCB_CONFIG_WINDOW_X) {
        client_pos.setX(requested_geo.x());
    }
    if (value_mask & XCB_CONFIG_WINDOW_Y) {
        client_pos.setY(requested_geo.y());
    }

    if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
        client_size.setWidth(requested_geo.width());
    }
    if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
        client_size.setHeight(requested_geo.height());
    }

    auto const frame_pos = win->control->rules.checkPosition(client_to_frame_pos(win, client_pos));
    auto const frame_size = size_for_client_size(win, client_size, size_mode::any, false);
    auto const frame_rect = QRect(frame_pos, frame_size);

    if (auto output = base::get_nearest_output(win->space.base.outputs, frame_rect.center());
        output != win->control->rules.checkScreen(win->space.base, output)) {
        // not allowed by rule
        return;
    }

    geometry_updates_blocker blocker(win);

    win->setFrameGeometry(frame_rect);

    auto area = space_window_area(win->space, WorkArea, win);

    if (!from_tool && (!is_special_window(win) || is_toolbar(win)) && !win->control->fullscreen
        && area.contains(frame_to_client_rect(win, frame_rect))) {
        keep_in_area(win, area, false);
    }
}

template<typename Win>
void configure_only_size_from_request(Win* win,
                                      QRect const& requested_geo,
                                      int& value_mask,
                                      int gravity,
                                      bool from_tool)
{
    auto const orig_client_geo = frame_to_client_rect(win, win->geometry_update.frame);
    auto client_size = orig_client_geo.size();

    if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
        client_size.setWidth(requested_geo.width());
    }
    if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
        client_size.setHeight(requested_geo.height());
    }

    geometry_updates_blocker blocker(win);
    resize_with_gravity(win, client_size, xcb_gravity_t(gravity));

    if (from_tool || (is_special_window(win) && !is_toolbar(win)) || win->control->fullscreen) {
        // All done.
        return;
    }

    // try to keep the window in its xinerama screen if possible,
    // if that fails at least keep it visible somewhere

    // TODO(romangg): If this is about Xinerama, can be removed?

    auto area = space_window_area(win->space, MovementArea, win);
    if (area.contains(orig_client_geo)) {
        keep_in_area(win, area, false);
    }

    area = space_window_area(win->space, WorkArea, win);
    if (area.contains(orig_client_geo)) {
        keep_in_area(win, area, false);
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

    if (configure_should_ignore(win, value_mask)) {
        qCDebug(KWIN_CORE) << "Configure request denied for: " << win;
        send_synthetic_configure_notify(win, win->synced_geometry.client);
        return;
    }

    if (gravity == 0) {
        // default (nonsense) value for the argument
        gravity = win->geometry_hints.window_gravity();
    }

    sync_suppressor sync_sup(win);

    if (value_mask & position_mask) {
        configure_position_size_from_request(win, requested_geo, value_mask, gravity, from_tool);
    }

    if (value_mask & size_mask && !(value_mask & position_mask)) {
        configure_only_size_from_request(win, requested_geo, value_mask, gravity, from_tool);
    }
}

template<typename Win>
void resize_with_gravity(Win* win, QSize const& size, xcb_gravity_t gravity)
{
    auto const tmp_size = constrain_and_adjust_size(win, size);
    auto width = tmp_size.width();
    auto height = tmp_size.height();

    if (gravity == 0) {
        gravity = win->geometry_hints.window_gravity();
    }

    auto pos_x = win->synced_geometry.frame.x();
    auto pos_y = win->synced_geometry.frame.y();

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

    win->setFrameGeometry(QRect(pos_x, pos_y, width, height));
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
bool update_server_geometry(Win* win, QRect const& frame_geo)
{
    // The render geometry defines the outer bounds of the window (that is with SSD or GTK CSD).
    auto const outer_geo = frame_to_render_rect(win, frame_geo);

    // Our wrapper geometry is in global coordinates the outer geometry excluding SSD.
    // That equals the the client geometry.
    auto const abs_wrapper_geo = outer_geo - frame_margins(win);
    assert(abs_wrapper_geo == frame_to_client_rect(win, frame_geo));

    // The wrapper is relatively positioned to the outer geometry.
    auto const rel_wrapper_geo = abs_wrapper_geo.translated(-outer_geo.topLeft());

    // Adding the original client frame extents does the same as frame_to_render_rect.
    auto const old_outer_geo
        = win->synced_geometry.client + win->geometry_update.original.deco_margins;

    auto const old_abs_wrapper_geo = old_outer_geo - win->geometry_update.original.deco_margins;

    auto const old_rel_wrapper_geo = old_abs_wrapper_geo.translated(-old_outer_geo.topLeft());

    win->synced_geometry.max_mode = win->geometry_update.max_mode;
    win->synced_geometry.fullscreen = win->geometry_update.fullscreen;

    if (old_outer_geo.size() != outer_geo.size() || old_rel_wrapper_geo != rel_wrapper_geo
        || !win->first_geo_synced) {
        win->xcb_windows.outer.set_geometry(outer_geo);
        win->xcb_windows.wrapper.set_geometry(rel_wrapper_geo);
        win->xcb_windows.client.resize(rel_wrapper_geo.size());

        update_shape(win);
        update_input_window(win, frame_geo);

        win->synced_geometry.frame = frame_geo;
        win->synced_geometry.client = abs_wrapper_geo;

        return true;
    }

    if (win->control->move_resize.enabled) {
        if (win->space.base.render->compositor->scene) {
            // Defer the X server update until we leave this mode.
            win->move_needs_server_update = true;
        } else {
            // sendSyntheticConfigureNotify() on finish shall be sufficient
            win->xcb_windows.outer.move(outer_geo.topLeft());
            win->synced_geometry.frame = frame_geo;
            win->synced_geometry.client = abs_wrapper_geo;
        }
    } else {
        win->xcb_windows.outer.move(outer_geo.topLeft());
        win->synced_geometry.frame = frame_geo;
        win->synced_geometry.client = abs_wrapper_geo;
    }

    win->xcb_windows.input.move(outer_geo.topLeft() + win->input_offset);
    return false;
}

template<typename Win>
void sync_geometry(Win* win, QRect const& frame_geo)
{
    auto const client_geo = frame_to_client_rect(win, frame_geo);

    assert(win->sync_request.counter != XCB_NONE);
    assert(win->synced_geometry.client != client_geo || !win->first_geo_synced);

    send_sync_request(win);
    win->pending_configures.push_back({
        win->sync_request.update_request_number,
        {frame_geo, client_geo, win->geometry_update.max_mode, win->geometry_update.fullscreen},
    });
}

/**
 * Calculates the bounding rectangle defined by the 4 monitor indices indicating the
 * top, bottom, left, and right edges of the window when the fullscreen state is enabled.
 */
template<typename Win>
QRect fullscreen_monitors_area(Win* win, NETFullscreenMonitors requestedTopology)
{
    QRect top, bottom, left, right, total;
    auto const& outputs = win->space.base.outputs;

    auto get_rect = [&outputs](auto index) -> QRect {
        auto output = base::get_output(outputs, index);
        return output ? output->geometry() : QRect();
    };
    top = get_rect(requestedTopology.top);
    bottom = get_rect(requestedTopology.bottom);
    left = get_rect(requestedTopology.left);
    right = get_rect(requestedTopology.right);

    total = top.united(bottom.united(left.united(right)));

    return total;
}

template<typename Win>
void update_fullscreen_monitors(Win* win, NETFullscreenMonitors topology)
{
    auto count = static_cast<int>(win->space.base.outputs.size());

    if (topology.top >= count || topology.bottom >= count || topology.left >= count
        || topology.right >= count) {
        qCWarning(KWIN_CORE)
            << "fullscreenMonitors update failed. request higher than number of screens.";
        return;
    }

    win->info->setFullscreenMonitors(topology);
    if (win->control->fullscreen) {
        win->setFrameGeometry(fullscreen_monitors_area(win, topology));
    }
}

template<typename Win>
NETExtendedStrut strut(Win const* win)
{
    NETExtendedStrut ext = win->info->extendedStrut();
    NETStrut str = win->info->strut();
    auto const displaySize = kwinApp()->get_base().topology.size;

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

    auto screenarea = space_window_area(win->space, ScreenArea, win);

    // HACK: workarea handling is not xinerama aware, so if this strut
    // reserves place at a xinerama edge that's inside the virtual screen,
    // ignore the strut for workspace setting.
    if (area == QRect({}, kwinApp()->get_base().topology.size)) {
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
strut_rect get_strut_rect(Win const* win, strut_area area)
{
    // Not valid
    assert(area != strut_area::all);

    auto const displaySize = kwinApp()->get_base().topology.size;
    NETExtendedStrut strutArea = strut(win);

    switch (area) {
    case strut_area::top:
        if (strutArea.top_width != 0)
            return strut_rect(QRect(strutArea.top_start,
                                    0,
                                    strutArea.top_end - strutArea.top_start,
                                    strutArea.top_width),
                              strut_area::top);
        break;
    case strut_area::right:
        if (strutArea.right_width != 0)
            return strut_rect(QRect(displaySize.width() - strutArea.right_width,
                                    strutArea.right_start,
                                    strutArea.right_width,
                                    strutArea.right_end - strutArea.right_start),
                              strut_area::right);
        break;
    case strut_area::bottom:
        if (strutArea.bottom_width != 0)
            return strut_rect(QRect(strutArea.bottom_start,
                                    displaySize.height() - strutArea.bottom_width,
                                    strutArea.bottom_end - strutArea.bottom_start,
                                    strutArea.bottom_width),
                              strut_area::bottom);
        break;
    case strut_area::left:
        if (strutArea.left_width != 0)
            return strut_rect(QRect(0,
                                    strutArea.left_start,
                                    strutArea.left_width,
                                    strutArea.left_end - strutArea.left_start),
                              strut_area::left);
        break;
    default:
        // Not valid
        abort();
    }

    return strut_rect();
}

template<typename Win>
strut_rects get_strut_rects(Win const* win)
{
    strut_rects region;
    region.push_back(get_strut_rect(win, strut_area::top));
    region.push_back(get_strut_rect(win, strut_area::right));
    region.push_back(get_strut_rect(win, strut_area::bottom));
    region.push_back(get_strut_rect(win, strut_area::left));
    return region;
}

template<typename Win>
bool has_offscreen_xinerama_strut(Win const* win)
{
    // Get strut as a QRegion
    QRegion region;
    region += get_strut_rect(win, strut_area::top);
    region += get_strut_rect(win, strut_area::right);
    region += get_strut_rect(win, strut_area::bottom);
    region += get_strut_rect(win, strut_area::left);

    auto const& outputs = win->space.base.outputs;

    // Remove all visible areas so that only the invisible remain
    for (auto output : outputs) {
        region -= output->geometry();
    }

    // If there's anything left then we have an offscreen strut
    return !region.isEmpty();
}

}
