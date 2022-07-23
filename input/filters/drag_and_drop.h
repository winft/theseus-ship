/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
#include "main.h"
#include "toplevel.h"
#include "win/activation.h"
#include "win/space.h"
#include "xwl/xwayland_interface.h"

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
        auto seat = waylandServer()->seat();
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
        auto seat = waylandServer()->seat();
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
        auto window = this->redirect.findManagedToplevel(pos.toPoint());
        if (auto xwl = xwayland()) {
            const auto ret = xwl->drag_move_filter(window, pos.toPoint());
            if (ret == xwl::drag_event_reply::ignore) {
                return false;
            } else if (ret == xwl::drag_event_reply::take) {
                return true;
            }
        }

        if (window) {
            // TODO: consider decorations
            if (window->surface != seat->drags().get_target().surface) {
                if (window->control) {
                    win::activate_window(this->redirect.space, window);
                }
                seat->drags().set_target(window->surface, window->input_transform());
            }
        } else {
            // No window at that place, if we have a surface we need to reset.
            seat->drags().set_target(nullptr);
        }

        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = waylandServer()->seat();
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
        this->redirect.touch()->insertId(event.id, seat->touches().touch_down(event.pos));
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        auto seat = waylandServer()->seat();
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
        const qint32 wraplandId = this->redirect.touch()->mappedId(event.id);
        if (wraplandId == -1) {
            return true;
        }

        seat->touches().touch_move(wraplandId, event.pos);

        if (auto t = this->redirect.findToplevel(event.pos.toPoint())) {
            // TODO: consider decorations
            if (t->surface != seat->drags().get_target().surface) {
                if (t->control) {
                    win::activate_window(this->redirect.space, t);
                }
                seat->drags().set_target(t->surface, event.pos, t->input_transform());
            }
        } else {
            // no window at that place, if we have a surface we need to reset
            seat->drags().set_target(nullptr);
        }
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        auto seat = waylandServer()->seat();
        if (!seat->drags().is_touch_drag()) {
            return false;
        }
        seat->setTimestamp(event.base.time_msec);
        const qint32 wraplandId = this->redirect.touch()->mappedId(event.id);
        if (wraplandId != -1) {
            seat->touches().touch_up(wraplandId);
            this->redirect.touch()->removeId(event.id);
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
