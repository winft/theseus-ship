/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "constants.h"

#include "render/types.h"

#include <QDateTime>
#include <QPair>

namespace KWin::render::post
{

using DateTimes = QPair<QDateTime, QDateTime>;
using Times = QPair<QTime, QTime>;

struct night_color_data {
    bool inhibited() const
    {
        return inhibit_reference_count;
    }

    /**
     * Returns the duration of the previous screen color temperature transition, in milliseconds.
     */
    qint64 previous_transition_duration() const
    {
        return transition.prev.first.msecsTo(transition.prev.second);
    }

    /**
     * Returns the duration of the next screen color temperature transition, in milliseconds.
     */
    qint64 scheduled_transition_duration() const
    {
        return transition.next.first.msecsTo(transition.next.second);
    }

    // TODO(romangg): That depended in the past on the hardware backend in use. But right now all
    //                backends support gamma control. We might remove this in the future.
    bool available{true};

    // Specifies whether Night Color is enabled.
    bool enabled{false};

    // Specifies whether Night Color is currently running.
    bool running{false};

    // Specifies whether Night Color is inhibited globally.
    bool globally_inhibited{false};

    night_color_mode mode{night_color_mode::automatic};

    /**
     * The next and previous sunrise/sunset intervals - in UTC time.
     *
     * The first element specifies when the previous/next color temperature transition started.
     * Notice that when Night Color operates in the Constant mode, this QDateTime object is invalid.
     */
    struct {
        DateTimes prev{DateTimes()};
        DateTimes next{DateTimes()};

        // Saved in minutes > 1
        int duration{30};
    } transition;

    // Whether it is currently day or night
    bool daylight{true};

    // Manual times from config
    struct {
        QTime morning{QTime(6, 0)};
        QTime evening{QTime(18, 0)};
    } man_time;

    // Auto location provided by work space
    struct {
        double lat;
        double lng;
    } auto_loc;

    // Manual location from config
    struct {
        double lat;
        double lng;
    } man_loc;

    struct {
        int current{DEFAULT_DAY_TEMPERATURE};
        int target{DEFAULT_DAY_TEMPERATURE};
        int day_target{DEFAULT_DAY_TEMPERATURE};
        int night_target{DEFAULT_NIGHT_TEMPERATURE};
    } temperature;

    int failed_commit_attempts{0};
    int inhibit_reference_count{0};
};

}
