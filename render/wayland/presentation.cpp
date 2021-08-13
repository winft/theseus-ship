/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "presentation.h"

#include "abstract_wayland_output.h"
#include "main.h"
#include "platform.h"
#include "toplevel.h"
#include "wayland_server.h"

#include "render/wayland/output.h"

#include <Wrapland/Server/output.h>
#include <Wrapland/Server/presentation_time.h>
#include <Wrapland/Server/surface.h>

#include <QElapsedTimer>

#define NSEC_PER_SEC 1000000000

namespace KWin::render::wayland
{

presentation::presentation(QObject* parent)
    : QObject(parent)
{
}

presentation::~presentation()
{
    delete fallback_clock;
}

bool presentation::init_clock(bool clockid_valid, clockid_t clockid)
{
    if (clockid_valid) {
        this->clockid = clockid;

        struct timespec ts;
        if (clock_gettime(clockid, &ts) != 0) {
            qCWarning(KWIN_CORE) << "Could not get presentation clock.";
            return false;
        }
    } else {
        // There might be other clock types, but for now assume it is always monotonic or realtime.
        clockid = QElapsedTimer::isMonotonic() ? CLOCK_MONOTONIC : CLOCK_REALTIME;

        fallback_clock = new QElapsedTimer();
        fallback_clock->start();
    }

    if (!waylandServer()->presentationManager()) {
        waylandServer()->createPresentationManager();
    }
    waylandServer()->presentationManager()->setClockId(clockid);

    return true;
}

uint32_t presentation::current_time() const
{
    if (fallback_clock) {
        return fallback_clock->elapsed();
    }

    uint32_t time{0};
    timespec ts;
    if (clock_gettime(clockid, &ts) == 0) {
        time = ts.tv_sec * 1000 + ts.tv_nsec / 1000 / 1000;
    }
    return time;
}

void presentation::lock(render::wayland::output* output, std::deque<Toplevel*> const& windows)
{
    auto const now = current_time();

    // TODO(romangg): what to do when the output gets removed or disabled while we have locked
    // surfaces?

    for (auto& win : windows) {
        auto surface = win->surface();
        if (!surface) {
            continue;
        }

        // Check if this window should be locked to the output. We use maximum coverage for that.
        auto const enabled_outputs = kwinApp()->platform()->enabledOutputs();
        auto max_out = enabled_outputs[0];
        int max_area = 0;

        auto const frame_geo = win->frameGeometry();
        for (auto out : enabled_outputs) {
            auto const intersect_geo = frame_geo.intersected(out->geometry());
            auto const area = intersect_geo.width() * intersect_geo.height();
            if (area > max_area) {
                max_area = area;
                max_out = out;
            }
        }

        if (max_out != output->base) {
            // Window not mostly on this output. We lock it to max_out when it presents.
            continue;
        }

        // TODO (romangg): Split this up to do on every subsurface (annexed transient) separately.
        surface->frameRendered(now);

        auto const id = surface->lockPresentation(output->base->output());
        if (id != 0) {
            output->assigned_surfaces.emplace(id, surface);
            connect(surface, &Wrapland::Server::Surface::resourceDestroyed, output, [output, id]() {
                output->assigned_surfaces.erase(id);
            });
        }
    }
}

Wrapland::Server::Surface::PresentationKinds to_kinds(presentation::kinds kinds)
{
    using kind = presentation::kind;
    using ret_kind = Wrapland::Server::Surface::PresentationKind;

    Wrapland::Server::Surface::PresentationKinds ret;
    if (kinds.testFlag(kind::Vsync)) {
        ret |= ret_kind::Vsync;
    }
    if (kinds.testFlag(kind::HwClock)) {
        ret |= ret_kind::HwClock;
    }
    if (kinds.testFlag(kind::HwCompletion)) {
        ret |= ret_kind::HwCompletion;
    }
    if (kinds.testFlag(kind::ZeroCopy)) {
        ret |= ret_kind::ZeroCopy;
    }
    return ret;
}

// From Weston.
void timespec_to_proto(const timespec& ts,
                       uint32_t& tv_sec_hi,
                       uint32_t& tv_sec_lo,
                       uint32_t& tv_n_sec)
{
    Q_ASSERT(ts.tv_sec >= 0);
    Q_ASSERT(ts.tv_nsec >= 0 && ts.tv_nsec < NSEC_PER_SEC);

    uint64_t sec64 = ts.tv_sec;

    tv_sec_hi = sec64 >> 32;
    tv_sec_lo = sec64 & 0xffffffff;
    tv_n_sec = ts.tv_nsec;
}

void presentation::presented(render::wayland::output* output,
                             uint32_t sec,
                             uint32_t usec,
                             kinds kinds)
{
    if (!output->base->isEnabled()) {
        // Output disabled, discards will be sent from Wrapland.
        return;
    }

    timespec ts;
    ts.tv_sec = sec;
    ts.tv_nsec = usec * 1000;

    uint32_t tv_sec_hi;
    uint32_t tv_sec_lo;
    uint32_t tv_n_sec;
    timespec_to_proto(ts, tv_sec_hi, tv_sec_lo, tv_n_sec);

    auto const refresh_rate = output->base->refreshRate();
    assert(refresh_rate > 0);

    auto const refresh_length = 1 / (double)refresh_rate;
    uint32_t const refresh = refresh_length * 1000 * 1000 * 1000 * 1000;
    auto const msc = output->base->msc();

    for (auto& [id, surface] : output->assigned_surfaces) {
        surface->presentationFeedback(id,
                                      tv_sec_hi,
                                      tv_sec_lo,
                                      tv_n_sec,
                                      refresh,
                                      msc >> 32,
                                      msc & 0xffffffff,
                                      to_kinds(kinds));
        disconnect(surface, &Wrapland::Server::Surface::resourceDestroyed, output, nullptr);
    }
    output->assigned_surfaces.clear();
}

void presentation::software_presented(kinds kinds)
{
    int64_t const elapsed_time = fallback_clock->nsecsElapsed();
    uint32_t const elapsed_seconds = static_cast<double>(elapsed_time) / NSEC_PER_SEC;
    uint32_t const nano_seconds_part
        = elapsed_time - static_cast<int64_t>(elapsed_seconds) * NSEC_PER_SEC;

    timespec ts;
    ts.tv_sec = elapsed_seconds;
    ts.tv_nsec = nano_seconds_part;

    uint32_t tv_sec_hi;
    uint32_t tv_sec_lo;
    uint32_t tv_n_sec;
    timespec_to_proto(ts, tv_sec_hi, tv_sec_lo, tv_n_sec);

    auto output = static_cast<AbstractWaylandOutput*>(kwinApp()->platform()->enabledOutputs()[0]);

    int const refresh_rate = output->refreshRate();
    assert(refresh_rate > 0);

    const double refresh_length = 1 / (double)refresh_rate;
    uint32_t const refresh = refresh_length * 1000 * 1000 * 1000 * 1000;

    uint64_t const seq = fallback_clock->elapsed() / (double)refresh_rate;

    auto it = surfaces.constBegin();
    while (it != surfaces.constEnd()) {
        auto surface = it.value();
        surface->presentationFeedback(it.key(),
                                      tv_sec_hi,
                                      tv_sec_lo,
                                      tv_n_sec,
                                      refresh,
                                      seq >> 32,
                                      seq & 0xffffffff,
                                      to_kinds(kinds));
        disconnect(surface, &Wrapland::Server::Surface::resourceDestroyed, this, nullptr);
        ++it;
    }
    surfaces.clear();
}

}
