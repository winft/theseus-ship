/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/seat/session.h"
#include "base/utils.h"

#include <vector>
#include <wayland-server-core.h>

struct wlr_backend;
struct wlr_device;
struct wlr_session;

namespace KWin::base::seat::backend::wlroots
{

class session;

class KWIN_EXPORT session : public seat::session
{
    Q_OBJECT
public:
    event_receiver<session> active_changed;
    event_receiver<session> destroyed;
    wlr_session* native{nullptr};
    std::vector<wlr_device*> taken_devices;

    explicit session(wlr_backend* backend);
    ~session() override;

    bool isConnected() const override;
    bool hasSessionControl() const override;
    bool isActiveSession() const override;
    int vt() const override;
    void switchVirtualTerminal(quint32 vtNr) override;

    int takeDevice(const char* path) override;
    void releaseDevice(int fd) override;

    const QString seat() const override;

    void take_control();
};

}
