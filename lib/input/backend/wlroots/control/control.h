/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/control/config.h"

#include <QSizeF>
#include <libinput.h>

namespace KWin::input::backend::wlroots
{

template<typename Dev>
void populate_metadata(Dev dev)
{
    auto& data = dev->metadata;
    data.name = libinput_device_get_name(dev->dev);
    data.sys_name = libinput_device_get_sysname(dev->dev);
    data.vendor_id = libinput_device_get_id_vendor(dev->dev);
    data.product_id = libinput_device_get_id_product(dev->dev);
}

template<typename Dev>
void init_device_control(Dev* dev, KSharedConfigPtr input_config)
{
    populate_metadata(dev);

    dev->config->group = input_config->group("Libinput")
                             .group(QString::number(dev->metadata.vendor_id))
                             .group(QString::number(dev->metadata.product_id))
                             .group(dev->metadata.name.c_str());

    control::load_config(dev);
}

template<typename Dev>
bool supports_disable_events_backend(Dev dev)
{
    return libinput_device_config_send_events_get_modes(dev->dev)
        & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
}

template<typename Dev>
bool is_enabled_backend(Dev dev)
{
    if (!dev->supports_disable_events()) {
        return true;
    }
    return libinput_device_config_send_events_get_mode(dev->dev)
        == LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
}

template<typename Dev>
bool set_enabled_backend(Dev dev, bool enabled)
{
    auto target
        = enabled ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
    return libinput_device_config_send_events_set_mode(dev->dev, target)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

template<typename Dev>
QSizeF size_backend(Dev dev)
{
    double width = 0;
    double height = 0;
    if (libinput_device_get_size(dev->dev, &width, &height) != 0) {
        return QSizeF();
    }
    return QSizeF(width, height);
}

}
