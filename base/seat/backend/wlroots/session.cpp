/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "session.h"

#include "main.h"
#include "wayland_server.h"

#include <wayland_logging.h>

#include <Wrapland/Server/display.h>

extern "C" {
#include <wlr/backend.h>
#include <wlr/backend/session.h>
}

namespace KWin::base::seat::backend::wlroots
{

session::session(wlr_backend* backend)
    : seat::session()
    , native{wlr_backend_get_session(backend)}
{
}

session::~session()
{
    if (native) {
        for (auto dev : taken_devices) {
            wlr_session_close_file(native, dev);
        }
        wlr_session_destroy(native);
    }
}

bool session::isConnected() const
{
    return true;
}

bool session::hasSessionControl() const
{
    return native;
}

bool session::isActiveSession() const
{
    return native && native->active;
}

int session::vt() const
{
    return native ? native->vtnr : -1;
}

void handle_active(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    event_receiver<session>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto session = event_receiver_struct->receiver;

    Q_EMIT session->sessionActiveChanged(session->native->active);
    Q_EMIT session->virtualTerminalChanged(session->native->vtnr);
}

void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    event_receiver<session>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto session = event_receiver_struct->receiver;

    session->native = nullptr;
}

void session::take_control()
{
    // TODO(romangg): assert instead?
    if (!native) {
        native = wlr_session_create(waylandServer()->display()->native());
        if (!native) {
            // TODO(romangg): error handling?
            qCCritical(KWIN_WL) << "Could not take control.";
            return;
        }
    }

    active_changed.receiver = this;
    active_changed.event.notify = handle_active;
    wl_signal_add(&native->events.active, &active_changed.event);

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&native->events.destroy, &destroyed.event);
}

int session::takeDevice(const char* path)
{
    auto device = wlr_session_open_file(native, path);
    if (!device) {
        qCDebug(KWIN_WL) << "Could not take device" << path;
        return -1;
    }
    taken_devices.push_back(device);
    return device->fd;
}

void session::releaseDevice(int fd)
{
    for (auto it = taken_devices.begin(); it != taken_devices.end(); it++) {
        if ((*it)->fd != fd) {
            continue;
        }
        wlr_session_close_file(native, *it);
        taken_devices.erase(it);
        break;
    }
}

void session::switchVirtualTerminal(quint32 vtNr)
{
    if (!native) {
        return;
    }
    wlr_session_change_vt(native, vtNr);
}

const QString session::seat() const
{
    if (!native) {
        return QString();
    }
    return native->seat;
}

}
