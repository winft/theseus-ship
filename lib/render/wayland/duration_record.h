/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <chrono>

namespace KWin::render::wayland
{

/**
 * Provides a record of by default 100 values with extra designated maximum value.
 */
struct duration_record {
    duration_record() = default;
    duration_record(int period_count)
        : period_count{period_count}
    {
    }

    std::chrono::nanoseconds get_max() const
    {
        if (durations[0] > durations[1]) {
            return durations[0];
        }
        return durations[1];
    }

    void update(std::chrono::nanoseconds duration)
    {
        if (duration > durations[1]) {
            durations[1] = duration;
        }

        periods++;

        // We take the maximum over the last frames.
        if (periods == period_count) {
            durations[0] = durations[1];
            durations[1] = std::chrono::nanoseconds::zero();
            periods = 0;
        }
    }

private:
    int period_count{100};
    std::chrono::nanoseconds durations[2]{};
    int periods{0};
};

}
