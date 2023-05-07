/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

namespace KWin::base
{

platform::platform(base::config config)
    : config{std::move(config)}
    , x11_event_filters{std::make_unique<base::x11::event_filter_manager>()}
{
    QObject::connect(this, &platform::output_added, this, [this](auto output) {
        if (!topology.current) {
            topology.current = output;
        }
    });
    QObject::connect(this, &platform::output_removed, this, [this](auto output) {
        if (output == topology.current) {
            topology.current = nullptr;
        }
    });
}

platform::~platform() = default;

clockid_t platform::get_clockid() const
{
    return CLOCK_MONOTONIC;
}

}
