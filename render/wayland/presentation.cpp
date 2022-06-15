/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "presentation.h"

#include "output.h"
#include "utils.h"

#include "base/wayland/output.h"
#include "base/wayland/server.h"
#include "main.h"
#include "toplevel.h"
#include "wayland_logging.h"

#include <QElapsedTimer>
#include <Wrapland/Server/output.h>
#include <Wrapland/Server/presentation_time.h>
#include <Wrapland/Server/surface.h>
#include <system_error>

#define NSEC_PER_SEC 1000000000

namespace KWin::render::wayland
{

presentation::presentation(clockid_t clockid)
    : presentation_manager{waylandServer()->display->createPresentationManager()}
    , clockid{clockid}
{
    struct timespec ts;
    if (auto ret = clock_gettime(clockid, &ts); ret != 0) {
        throw std::system_error(ret, std::generic_category(), "Could not get presentation clock.");
    }
    presentation_manager->setClockId(clockid);
}
std::chrono::milliseconds get_now_in_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
}

void presentation::frame(render::wayland::output* output, std::deque<Toplevel*> const& windows)
{
    auto const now = get_now_in_ms().count();

    for (auto& win : windows) {
        assert(win->surface);
        assert(max_coverage_output(win) == &output->base);

        // TODO (romangg): Split this up to do on every subsurface (annexed transient) separately.
        win->surface->frameRendered(now);
    }
}

void presentation::lock(render::wayland::output* output, std::deque<Toplevel*> const& windows)
{
    auto const now = get_now_in_ms().count();

    // TODO(romangg): what to do when the output gets removed or disabled while we have locked
    // surfaces?

    for (auto& win : windows) {
        auto surface = win->surface;
        if (!surface) {
            continue;
        }

        // Check if this window should be locked to the output. We use maximum coverage for that.
        auto max_out = max_coverage_output(win);
        if (max_out != &output->base) {
            // Window not mostly on this output. We lock it to max_out when it presents.
            continue;
        }

        // TODO (romangg): Split this up to do on every subsurface (annexed transient) separately.
        surface->frameRendered(now);

        auto const id = surface->lockPresentation(output->base.wrapland_output());
        if (id != 0) {
            output->assigned_surfaces.emplace(id, surface);
            connect(surface, &Wrapland::Server::Surface::resourceDestroyed, output, [output, id]() {
                output->assigned_surfaces.erase(id);
            });
        }
    }
}

Wrapland::Server::Surface::PresentationKinds to_kinds(presentation_kinds kinds)
{
    using kind = presentation_kind;
    using ret_kind = Wrapland::Server::Surface::PresentationKind;

    Wrapland::Server::Surface::PresentationKinds ret;
    if (kinds.testFlag(kind::vsync)) {
        ret |= ret_kind::Vsync;
    }
    if (kinds.testFlag(kind::hw_clock)) {
        ret |= ret_kind::HwClock;
    }
    if (kinds.testFlag(kind::hw_completion)) {
        ret |= ret_kind::HwCompletion;
    }
    if (kinds.testFlag(kind::zero_copy)) {
        ret |= ret_kind::ZeroCopy;
    }
    return ret;
}

std::tuple<std::chrono::seconds, std::chrono::nanoseconds>
get_timespec_decomposition(std::chrono::nanoseconds time)
{
    auto const sec = std::chrono::duration_cast<std::chrono::seconds>(time);
    return {sec, time - sec};
}

// From Weston.
void timespec_to_proto(std::chrono::nanoseconds const& time,
                       uint32_t& tv_sec_hi,
                       uint32_t& tv_sec_lo,
                       uint32_t& tv_n_sec)
{
    auto [time_sec, time_nsec] = get_timespec_decomposition(time);

    uint64_t const sec64 = time_sec.count();
    tv_sec_hi = sec64 >> 32;
    tv_sec_lo = sec64 & 0xffffffff;
    tv_n_sec = time_nsec.count();
}

void presentation::presented(render::wayland::output* output, presentation_data const& data)
{
    if (!output->base.is_enabled()) {
        // Output disabled, discards will be sent from Wrapland.
        return;
    }

    uint32_t tv_sec_hi;
    uint32_t tv_sec_lo;
    uint32_t tv_n_sec;
    timespec_to_proto(data.when, tv_sec_hi, tv_sec_lo, tv_n_sec);

    uint64_t msc = data.seq;

    for (auto& [id, surface] : output->assigned_surfaces) {
        surface->presentationFeedback(id,
                                      tv_sec_hi,
                                      tv_sec_lo,
                                      tv_n_sec,
                                      data.refresh.count(),
                                      msc >> 32,
                                      msc & 0xffffffff,
                                      to_kinds(data.flags));
        disconnect(surface, &Wrapland::Server::Surface::resourceDestroyed, output, nullptr);
    }
    output->assigned_surfaces.clear();
}

}
