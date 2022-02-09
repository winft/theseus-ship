/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wrapper.h"

#include <QRect>
#include <xcb/randr.h>

namespace KWin::base::x11::xcb::randr
{

XCB_WRAPPER(screen_info, xcb_randr_get_screen_info, xcb_window_t)

XCB_WRAPPER_DATA(screen_resources_data, xcb_randr_get_screen_resources, xcb_window_t)
class screen_resources : public wrapper<screen_resources_data, xcb_window_t>
{
public:
    explicit screen_resources(xcb_window_t window)
        : wrapper<screen_resources_data, xcb_window_t>(window)
    {
    }

    inline xcb_randr_crtc_t* crtcs()
    {
        if (is_null()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_crtcs(data());
    }
    inline xcb_randr_mode_info_t* modes()
    {
        if (is_null()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_modes(data());
    }
    inline uint8_t* names()
    {
        if (is_null()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_names(data());
    }
};

XCB_WRAPPER_DATA(crtc_gamma_data, xcb_randr_get_crtc_gamma, xcb_randr_crtc_t)
class crtc_gamma : public wrapper<crtc_gamma_data, xcb_randr_crtc_t>
{
public:
    explicit crtc_gamma(xcb_randr_crtc_t c)
        : wrapper<crtc_gamma_data, xcb_randr_crtc_t>(c)
    {
    }

    inline uint16_t* red()
    {
        return xcb_randr_get_crtc_gamma_red(data());
    }
    inline uint16_t* green()
    {
        return xcb_randr_get_crtc_gamma_green(data());
    }
    inline uint16_t* blue()
    {
        return xcb_randr_get_crtc_gamma_blue(data());
    }
};

XCB_WRAPPER_DATA(crtc_info_data, xcb_randr_get_crtc_info, xcb_randr_crtc_t, xcb_timestamp_t)
class crtc_info : public wrapper<crtc_info_data, xcb_randr_crtc_t, xcb_timestamp_t>
{
public:
    crtc_info() = default;
    crtc_info(crtc_info const&) = default;
    explicit crtc_info(xcb_randr_crtc_t c, xcb_timestamp_t t)
        : wrapper<crtc_info_data, xcb_randr_crtc_t, xcb_timestamp_t>(c, t)
    {
    }

    inline QRect rect()
    {
        const crtc_info_data::reply_type* info = data();
        if (!info || info->num_outputs == 0 || info->mode == XCB_NONE
            || info->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            return QRect();
        }
        return QRect(info->x, info->y, info->width, info->height);
    }
    inline xcb_randr_output_t* outputs()
    {
        const crtc_info_data::reply_type* info = data();
        if (!info || info->num_outputs == 0 || info->mode == XCB_NONE
            || info->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            return nullptr;
        }
        return xcb_randr_get_crtc_info_outputs(info);
    }
};

XCB_WRAPPER_DATA(output_info_data, xcb_randr_get_output_info, xcb_randr_output_t, xcb_timestamp_t)
class output_info : public wrapper<output_info_data, xcb_randr_output_t, xcb_timestamp_t>
{
public:
    output_info() = default;
    output_info(output_info const&) = default;
    explicit output_info(xcb_randr_output_t c, xcb_timestamp_t t)
        : wrapper<output_info_data, xcb_randr_output_t, xcb_timestamp_t>(c, t)
    {
    }

    inline QString name()
    {
        const output_info_data::reply_type* info = data();
        if (!info || info->num_crtcs == 0 || info->num_modes == 0
            || info->status != XCB_RANDR_SET_CONFIG_SUCCESS) {
            return QString();
        }
        return QString::fromUtf8(reinterpret_cast<char*>(xcb_randr_get_output_info_name(info)),
                                 info->name_len);
    }
};

XCB_WRAPPER_DATA(current_resources_data, xcb_randr_get_screen_resources_current, xcb_window_t)
class current_resources : public wrapper<current_resources_data, xcb_window_t>
{
public:
    explicit current_resources(xcb_window_t window)
        : wrapper<current_resources_data, xcb_window_t>(window)
    {
    }

    inline xcb_randr_crtc_t* crtcs()
    {
        if (is_null()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_current_crtcs(data());
    }
    inline xcb_randr_mode_info_t* modes()
    {
        if (is_null()) {
            return nullptr;
        }
        return xcb_randr_get_screen_resources_current_modes(data());
    }
};

XCB_WRAPPER(set_crtc_config,
            xcb_randr_set_crtc_config,
            xcb_randr_crtc_t,
            xcb_timestamp_t,
            xcb_timestamp_t,
            int16_t,
            int16_t,
            xcb_randr_mode_t,
            uint16_t,
            uint32_t,
            const xcb_randr_output_t*)
}
