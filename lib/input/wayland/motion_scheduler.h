/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QPointF>
#include <cstdint>
#include <deque>

namespace KWin::input::wayland
{

template<typename Device>
class motion_scheduler
{
public:
    motion_scheduler(Device& device)
        : device{device}
    {
    }

    void lock()
    {
        ++locked;
    }

    void unlock()
    {
        if (--locked > 0) {
            // Still locked.
            return;
        }
        if (motions.empty()) {
            // No motions logged.
            return;
        }

        auto const sched = motions.front();
        motions.pop_front();
        if (sched.abs) {
            device.process_motion_absolute({sched.pos, {nullptr, sched.time}});
        } else {
            device.process_motion({sched.delta, sched.unaccel_delta, {nullptr, sched.time}});
        }
    }

    bool is_locked()
    {
        return locked > 0;
    }

    void schedule(QPointF const& pos, uint32_t time)
    {
        motions.emplace_back(position{pos, {}, {}, time, true});
    }

    void schedule(QPointF const& delta, QPointF const& unaccel_delta, uint32_t time)
    {
        motions.emplace_back(position{{}, delta, unaccel_delta, time, false});
    }

private:
    struct position {
        QPointF pos;
        QPointF delta;
        QPointF unaccel_delta;
        uint32_t time;
        bool abs;
    };

    std::deque<position> motions;
    int locked{0};
    Device& device;
};

}
