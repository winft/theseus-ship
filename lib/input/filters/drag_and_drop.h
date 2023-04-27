/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/window_find.h"
#include "win/activation.h"
#include "xwl/types.h"

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

template<typename Redirect>
class drag_and_drop_filter : public event_filter<Redirect>
{
public:
    explicit drag_and_drop_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        auto seat = this->redirect.platform.base.server->seat();
        if (!seat->drags().is_pointer_drag()) {
            return false;
        }
        if (seat->drags().is_touch_drag()) {
            return true;
        }
        seat->setTimestamp(event.base.time_msec);

        if (event.state == button_state::pressed) {
            seat->pointers().button_pressed(event.key);
        } else {
            seat->pointers().button_released(event.key);
        }

        return true;
    }

    bool motion(motion_event const& event) override
    {
        auto seat = this->redirect.platform.base.server->seat();
        if (!seat->drags().is_pointer_drag()) {
            return false;
        }
        if (seat->drags().is_touch_drag()) {
            return true;
        }
        seat->setTimestamp(event.base.time_msec);

        auto const pos = this->redirect.globalPointer();
        seat->pointers().set_position(pos);

        // TODO: use InputDeviceHandler::at() here and check isClient()?
        auto window = find_controlled_window(this->redirect, pos.toPoint());
        if (auto& xwl = this->redirect.platform.base.xwayland) {
            const auto ret = xwl->drag_move_filter(window, pos.toPoint());
            if (ret == xwl::drag_event_reply::ignore) {
                return false;
            } else if (ret == xwl::drag_event_reply::take) {
                return true;
            }
        }

        if (!window) {
            // No window at that place, if we have a surface we need to reset.
            seat->drags().set_target(nullptr);
            return true;
        }

        std::visit(overload{[&](auto&& win) {
                       // TODO: consider decorations
                       if constexpr (requires(decltype(win) win) { win->surface; }) {
                           if (win->surface == seat->drags().get_target().surface) {
                               return;
                           }
                           if (win->control) {
                               win::activate_window(this->redirect.space, *win);
                           }
                           seat->drags().set_target(win->surface, win::get_input_transform(*win));
                       } else {
                           seat->drags().set_target(nullptr, win::get_input_transform(*win));
                       }
                   }},
                   *window);

        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = this->redirect.platform.base.server->seat();
        if (seat->drags().is_pointer_drag()) {
            return true;
        }
        if (!seat->drags().is_touch_drag()) {
            return false;
        }
        if (m_touchId != event.id) {
            return true;
        }
        seat->setTimestamp(event.base.time_msec);
        this->redirect.touch->insertId(event.id, seat->touches().touch_down(event.pos));
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        auto seat = this->redirect.platform.base.server->seat();
        if (seat->drags().is_pointer_drag()) {
            return true;
        }
        if (!seat->drags().is_touch_drag()) {
            return false;
        }
        if (m_touchId < 0) {
            // We take for now the first id appearing as a move after a drag
            // started. We can optimize by specifying the id the drag is
            // associated with by implementing a key-value getter in Wrapland.
            m_touchId = event.id;
        }
        if (m_touchId != event.id) {
            return true;
        }
        seat->setTimestamp(event.base.time_msec);
        const qint32 wraplandId = this->redirect.touch->mappedId(event.id);
        if (wraplandId == -1) {
            return true;
        }

        seat->touches().touch_move(wraplandId, event.pos);

        auto win = find_window(this->redirect, event.pos.toPoint());
        if (!win) {
            // no window at that place, if we have a surface we need to reset
            seat->drags().set_target(nullptr);
            return true;
        }

        // TODO: consider decorations
        std::visit(overload{[&](auto&& win) {
                       if constexpr (requires(decltype(win) win) { win->surface; }) {
                           if (win->surface == seat->drags().get_target().surface) {
                               return;
                           }
                           if (win->control) {
                               win::activate_window(this->redirect.space, *win);
                           }
                           seat->drags().set_target(win->surface, win::get_input_transform(*win));
                       } else {
                           seat->drags().set_target(nullptr, win::get_input_transform(*win));
                       }
                   }},
                   *win);
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        auto seat = this->redirect.platform.base.server->seat();
        if (!seat->drags().is_touch_drag()) {
            return false;
        }
        seat->setTimestamp(event.base.time_msec);
        const qint32 wraplandId = this->redirect.touch->mappedId(event.id);
        if (wraplandId != -1) {
            seat->touches().touch_up(wraplandId);
            this->redirect.touch->removeId(event.id);
        }
        if (m_touchId == event.id) {
            m_touchId = -1;
        }
        return true;
    }

private:
    qint32 m_touchId = -1;
};

}
