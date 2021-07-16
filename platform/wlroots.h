/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "platform/utils.h"

extern "C" {
#include <wlr/backend.h>
#include <wlr/util/log.h>
}

namespace Wrapland::Server
{
class Display;
}

namespace KWin::platform_base
{

class wlroots
{
public:
    wlr_backend* backend{nullptr};

    // TODO(romangg): remove all but one ctor once the startup sequence is cleaned up.
    wlroots() = default;
    explicit wlroots(Wrapland::Server::Display* display);
    explicit wlroots(wlr_backend* backend);
    void init(wlr_backend* backend);

    wlroots(wlroots const&) = delete;
    wlroots& operator=(wlroots const&) = delete;
    wlroots(wlroots&& other) noexcept;
    wlroots& operator=(wlroots&& other) noexcept;
    ~wlroots();

    wlr_session* session() const
    {
        return wlr_backend_get_session(backend);
    }

private:
    event_receiver<wlroots> destroyed;
};

}
