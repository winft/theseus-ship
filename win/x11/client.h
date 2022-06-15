/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "input.h"
#include "netinfo.h"
#include "window.h"

#include "base/logging.h"
#include "win/meta.h"
#include "win/setup.h"

#include <xcb/sync.h>
#include <xcb/xcb_icccm.h>

#include <csignal>

namespace KWin::win::x11
{

/**
 * Sets the window's mapping state. Possible values are: WithdrawnState, IconicState, NormalState.
 */
template<typename Win>
void export_mapping_state(Win* win, int state)
{
    assert(win->xcb_windows.client != XCB_WINDOW_NONE);
    assert(!win->deleting || state == XCB_ICCCM_WM_STATE_WITHDRAWN);

    auto& atoms = win->space.atoms;

    if (state == XCB_ICCCM_WM_STATE_WITHDRAWN) {
        win->xcb_windows.client.delete_property(atoms->wm_state);
        return;
    }

    assert(state == XCB_ICCCM_WM_STATE_NORMAL || state == XCB_ICCCM_WM_STATE_ICONIC);

    int32_t data[2];
    data[0] = state;
    data[1] = XCB_NONE;
    win->xcb_windows.client.change_property(atoms->wm_state, atoms->wm_state, 32, 2, data);
}

inline void send_client_message(xcb_window_t w,
                                xcb_atom_t a,
                                xcb_atom_t protocol,
                                uint32_t data1 = 0,
                                uint32_t data2 = 0,
                                uint32_t data3 = 0)
{
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = w;
    ev.type = a;
    ev.format = 32;
    ev.data.data32[0] = protocol;
    ev.data.data32[1] = xTime();
    ev.data.data32[2] = data1;
    ev.data.data32[3] = data2;
    ev.data.data32[4] = data3;
    uint32_t eventMask = 0;

    if (w == rootWindow()) {
        eventMask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
    }

    xcb_send_event(connection(), false, w, eventMask, reinterpret_cast<const char*>(&ev));
    xcb_flush(connection());
}

/**
 * Send a ping to the window using _NET_WM_PING if possible if it
 * doesn't respond within a reasonable time, it will be killed.
 */
template<typename Win>
void ping(Win* win)
{
    if (!win->info->supportsProtocol(NET::PingProtocol)) {
        // Can't ping :(
        return;
    }
    if (kwinApp()->options->killPingTimeout() == 0) {
        // Turned off
        return;
    }
    if (win->ping_timer != nullptr) {
        // Pinging already
        return;
    }

    win->ping_timer = new QTimer(win);

    QObject::connect(win->ping_timer, &QTimer::timeout, win, [win]() {
        if (win->control->unresponsive()) {
            qCDebug(KWIN_CORE) << "Final ping timeout, asking to kill:" << win::caption(win);
            win->ping_timer->deleteLater();
            win->ping_timer = nullptr;
            kill_process(win, true, win->ping_timestamp);
            return;
        }

        qCDebug(KWIN_CORE) << "First ping timeout:" << win::caption(win);

        win->control->set_unresponsive(true);
        win->ping_timer->start();
    });

    win->ping_timer->setSingleShot(true);

    // We'll run the timer twice, at first we'll desaturate the window
    // and the second time we'll show the "do you want to kill" prompt.
    win->ping_timer->start(kwinApp()->options->killPingTimeout() / 2);

    win->ping_timestamp = xTime();
    rootInfo()->sendPing(win->xcb_window(), win->ping_timestamp);
}

template<typename Win>
void pong(Win* win, xcb_timestamp_t timestamp)
{
    // Just plain compare is not good enough because of 64bit and truncating and whatnot
    if (NET::timestampCompare(timestamp, win->ping_timestamp) != 0) {
        return;
    }

    delete win->ping_timer;
    win->ping_timer = nullptr;

    win->control->set_unresponsive(false);

    if (win->kill_helper_pid && !::kill(win->kill_helper_pid, 0)) { // means the process is alive
        ::kill(win->kill_helper_pid, SIGTERM);
        win->kill_helper_pid = 0;
    }
}

template<typename Win>
void kill_process(Win* win, bool ask, xcb_timestamp_t timestamp = XCB_TIME_CURRENT_TIME)
{
    if (win->kill_helper_pid && !::kill(win->kill_helper_pid, 0)) {
        // means the process is alive
        return;
    }

    assert(!ask || timestamp != XCB_TIME_CURRENT_TIME);

    auto pid = win->info->pid();
    if (pid <= 0 || win->client_machine->hostname().isEmpty()) {
        // Needed properties missing
        return;
    }

    qCDebug(KWIN_CORE) << "Kill process:" << pid << "(" << win->client_machine->hostname() << ")";

    if (!ask) {
        if (!win->client_machine->is_local()) {
            QStringList lst;
            lst << QString::fromUtf8(win->client_machine->hostname()) << QStringLiteral("kill")
                << QString::number(pid);
            QProcess::startDetached(QStringLiteral("xon"), lst);
        } else {
            ::kill(pid, SIGTERM);
        }
    } else {
        auto hostname = win->client_machine->is_local()
            ? QStringLiteral("localhost")
            : QString::fromUtf8(win->client_machine->hostname());
        // execute helper from build dir or the system installed one
        QFileInfo const buildDirBinary{QDir{QCoreApplication::applicationDirPath()},
                                       QStringLiteral("kwin_killer_helper")};
        QProcess::startDetached(buildDirBinary.exists() ? buildDirBinary.absoluteFilePath()
                                                        : QStringLiteral(KWIN_KILLER_BIN),
                                QStringList()
                                    << QStringLiteral("--pid") << QString::number(unsigned(pid))
                                    << QStringLiteral("--hostname") << hostname
                                    << QStringLiteral("--windowname") << win->caption.normal
                                    << QStringLiteral("--applicationname")
                                    << QString::fromUtf8(win->resource_class)
                                    << QStringLiteral("--wid") << QString::number(win->xcb_window())
                                    << QStringLiteral("--timestamp") << QString::number(timestamp),
                                QString(),
                                &win->kill_helper_pid);
    }
}

inline bool wants_sync_counter()
{
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        return true;
    }
    // When the frame window is resized, the attached buffer will be destroyed by
    // Xwayland, causing unexpected invalid previous and current window pixmaps.
    // With the addition of multiple window buffers in Xwayland 1.21, X11 clients
    // are no longer able to destroy the buffer after it's been committed and not
    // released by the compositor yet.
    static auto const xwayland_version = xcb_get_setup(connection())->release_number;
    return xwayland_version > 12099000;
}

template<typename Win>
void get_sync_counter(Win* win)
{
    if (!base::x11::xcb::extensions::self()->is_sync_available()) {
        return;
    }
    if (!wants_sync_counter()) {
        return;
    }

    base::x11::xcb::property syncProp(false,
                                      win->xcb_window(),
                                      win->space.atoms->net_wm_sync_request_counter,
                                      XCB_ATOM_CARDINAL,
                                      0,
                                      1);
    auto const counter = syncProp.value<xcb_sync_counter_t>(XCB_NONE);

    if (counter == XCB_NONE) {
        // Window without support for _NET_WM_SYNC_REQUEST.
        return;
    }

    auto con = connection();
    xcb_sync_set_counter(con, counter, {0, 0});
    win->sync_request.counter = counter;

    if (win->sync_request.alarm != XCB_NONE) {
        // Alarm exists already.
        // TODO(romangg): Instead assert that this does not happen or recreate alarm?
        return;
    }

    uint32_t const mask
        = XCB_SYNC_CA_COUNTER | XCB_SYNC_CA_VALUE_TYPE | XCB_SYNC_CA_TEST_TYPE | XCB_SYNC_CA_EVENTS;

    // TODO(romangg): XCB_SYNC_VALUETYPE_ABSOLUTE?
    uint32_t const values[]
        = {counter, XCB_SYNC_VALUETYPE_RELATIVE, XCB_SYNC_TESTTYPE_POSITIVE_COMPARISON, 1};

    auto const alarm_id = xcb_generate_id(con);
    auto cookie = xcb_sync_create_alarm_checked(con, alarm_id, mask, values);
    unique_cptr<xcb_generic_error_t> error(xcb_request_check(con, cookie));

    if (error) {
        qCWarning(KWIN_CORE) << "Error creating _NET_WM_SYNC_REQUEST alarm for: " << win;
        return;
    }

    xcb_sync_change_alarm_value_list_t value;
    memset(&value, 0, sizeof(value));
    value.value.hi = 0;
    value.value.lo = 1;
    value.delta.hi = 0;
    value.delta.lo = 1;
    xcb_sync_change_alarm_aux(con, alarm_id, XCB_SYNC_CA_DELTA | XCB_SYNC_CA_VALUE, &value);

    win->sync_request.alarm = alarm_id;
}

/**
 * Sends the client a _NET_SYNC_REQUEST.
 */
template<typename Win>
void send_sync_request(Win* win)
{
    assert(win->sync_request.counter != XCB_NONE);

    // We increment before the notify so that after the notify sync_request.update_request_number
    // equals the value we are expecting in the acknowledgement.
    win->sync_request.update_request_number++;
    if (win->sync_request.update_request_number == 0) {
        // Protection against wrap-around.
        win->sync_request.update_request_number++;
    }

    if (win->sync_request.timestamp >= xTime()) {
        kwinApp()->update_x11_time_from_clock();
    }

    auto const number_lo = (win->sync_request.update_request_number << 32) >> 32;
    auto const number_hi = win->sync_request.update_request_number >> 32;

    // Send the message to client
    auto& atoms = win->space.atoms;
    send_client_message(
        win->xcb_window(), atoms->wm_protocols, atoms->net_wm_sync_request, number_lo, number_hi);

    win->sync_request.timestamp = xTime();
}

/**
 * Auxiliary function to inform the client about the current window
 * configuration.
 */
template<typename Win>
void send_synthetic_configure_notify(Win* win, QRect const& client_geo)
{
    xcb_configure_notify_event_t c;
    memset(&c, 0, sizeof(c));

    c.response_type = XCB_CONFIGURE_NOTIFY;
    c.event = win->xcb_window();
    c.window = win->xcb_window();
    c.x = client_geo.x();
    c.y = client_geo.y();

    c.width = client_geo.width();
    c.height = client_geo.height();
    auto getEmulatedXWaylandSize = [win, &client_geo]() {
        auto property = base::x11::xcb::property(false,
                                                 win->xcb_window(),
                                                 win->space.atoms->xwayland_randr_emu_monitor_rects,
                                                 XCB_ATOM_CARDINAL,
                                                 0,
                                                 1000);
        if (!property) {
            return QSize();
        }
        auto rects = property.value<uint32_t*>();

        if (property->value_len % 4) {
            return QSize();
        }

        for (uint32_t i = 0; i < property->value_len / 4; i++) {
            auto r = &rects[i];

            if (r[0] - client_geo.x() == 0 && r[1] - client_geo.y() == 0) {
                return QSize(r[2], r[3]);
            }
        }
        return QSize();
    };

    if (win->control->fullscreen()) {
        // Workaround for XWayland clients setting fullscreen
        auto const emulatedSize = getEmulatedXWaylandSize();

        if (emulatedSize.isValid()) {
            c.width = emulatedSize.width();
            c.height = emulatedSize.height();

            uint32_t const values[] = {c.width, c.height};
            unique_cptr<xcb_generic_error_t> error(xcb_request_check(
                connection(),
                xcb_configure_window_checked(connection(),
                                             c.window,
                                             XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                                             values)));
            if (error) {
                qCDebug(KWIN_CORE) << "Error on emulating XWayland size: " << error->error_code;
            }
        }
    }

    c.border_width = 0;
    c.above_sibling = XCB_WINDOW_NONE;
    c.override_redirect = 0;

    xcb_send_event(connection(),
                   true,
                   c.event,
                   XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                   reinterpret_cast<const char*>(&c));
    xcb_flush(connection());
}

}
