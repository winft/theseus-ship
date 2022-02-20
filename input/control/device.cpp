/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device.h"

#include "config.h"
#include "input/platform.h"

#include "config-kwin.h"

namespace KWin::input::control
{

device::device(device_config* config, input::platform* plat)
    : QObject(nullptr)
    , plat{plat}
{
    this->config.reset(config);
}

device::~device() = default;

void device::init_config()
{
    config->group = plat->config->group("Libinput")
                        .group(QString::number(metadata.vendor_id))
                        .group(QString::number(metadata.product_id))
                        .group(metadata.name.c_str());
    load_config(this);
}

void device::set_enabled(bool enable)
{
    if (!supports_disable_events()) {
        return;
    }
    auto was_enabled = is_enabled();
    if (set_enabled_impl(enable) && was_enabled != enable) {
        write_entry(this, config_key::enabled, enable);
        Q_EMIT enabled_changed();
    }
}

}
