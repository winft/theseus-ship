/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"

#include <Wrapland/Server/drm_lease_v1.h>

struct wlr_output;

namespace KWin::base::backend::wlroots
{

class drm_lease;
class platform;

class non_desktop_output
{
public:
    non_desktop_output(wlr_output* wlr_out, wlroots::platform* platform);
    ~non_desktop_output();

    drm_lease* lease{nullptr};
    wlr_output* native;
    wlroots::platform* platform;

private:
    void create_lease_connector();

    std::unique_ptr<Wrapland::Server::drm_lease_connector_v1> lease_connector;
    base::event_receiver<non_desktop_output> destroy_rec;
};

}
