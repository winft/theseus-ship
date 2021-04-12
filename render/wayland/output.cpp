/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "abstract_wayland_output.h"
#include "composite.h"
#include "effects.h"
#include "platform.h"
#include "presentation.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/transient.h"
#include <kwingltexture.h>

#include "perf/ftrace.h"

namespace KWin::render::wayland
{

static int s_index{0};

output::output(AbstractWaylandOutput* base, WaylandCompositor* compositor)
    : index{++s_index}
    , compositor{compositor}
    , base{base}
{
}

void output::add_repaint(QRegion const& region)
{
    auto const capped_region = region.intersected(base->geometry());
    if (capped_region.isEmpty()) {
        return;
    }
    repaints_region += capped_region;
    set_delay_timer();
}

bool output::prepare_run(QRegion& repaints, std::deque<Toplevel*>& windows)
{
    delay_timer.stop();

    // If a buffer swap is still pending, we return to the event loop and
    // continue processing events until the swap has completed.
    if (swap_pending) {
        return false;
    }
    if (!kwinApp()->platform()->areOutputsEnabled()) {
        // TODO(romangg): This check is necessary at the moment because of Platform internals
        //                but should go away or be replaced with an output-specific check.
        return false;
    }

    // Create a list of all windows in the stacking order
    windows = Workspace::self()->xStackingOrder();
    bool has_window_repaints{false};

    for (auto win : windows) {
        if (win->has_pending_repaints()) {
            has_window_repaints = true;
            auto const repaint_region = win->repaints_region;

            for (auto& [base, output] : compositor->outputs) {
                if (output.get() == this) {
                    continue;
                }
                auto const capped_region = win->repaints_region.intersected(base->geometry());
                if (!capped_region.isEmpty()) {
                    output->add_repaint(capped_region);
                }
            }
        }
        if (win->resetAndFetchDamage()) {
            // Discard the cached lanczos texture
            if (win->transient()->annexed) {
                win = win::lead_of_annexed_transient(win);
            }
            if (win->effectWindow()) {
                auto const texture = win->effectWindow()->data(LanczosCacheRole);
                if (texture.isValid()) {
                    delete static_cast<GLTexture*>(texture.value<void*>());
                    win->effectWindow()->setData(LanczosCacheRole, QVariant());
                }
            }
        }
    }

    // TODO(romangg): Remove all windows not intersecting output. How to handle the visual geometry
    //                transforming effects like wobbly windows?

    // Move elevated windows to the top of the stacking order
    for (auto effect_window : static_cast<EffectsHandlerImpl*>(effects)->elevatedWindows()) {
        auto window = static_cast<EffectWindowImpl*>(effect_window)->window();
        remove_all(windows, window);
        windows.push_back(window);
    }

    if (repaints_region.isEmpty() && !has_window_repaints) {
        idle = true;
        compositor->check_idle();

        // This means the next time we composite it is done without timer delay.
        delay = 0;
        return false;
    }

    idle = false;

    // Skip windows that are not yet ready for being painted and if screen is locked skip windows
    // that are neither lockscreen nor inputmethod windows.
    //
    // TODO? This cannot be used so carelessly - needs protections against broken clients, the
    // window should not get focus before it's displayed, handle unredirected windows properly and
    // so on.
    for (auto win : windows) {
        if (!win->readyForPainting()) {
            windows.erase(std::remove(windows.begin(), windows.end(), win), windows.end());
        }
        if (waylandServer()->isScreenLocked() && !win->isLockScreen() && !win->isInputMethod()) {
            windows.erase(std::remove(windows.begin(), windows.end(), win), windows.end());
        }
    }

    repaints = repaints_region;

    // clear all repaints, so that post-pass can add repaints for the next repaint
    repaints_region = QRegion();

    return true;
}

std::deque<Toplevel*> output::run()
{
    QRegion repaints;
    std::deque<Toplevel*> windows;

    if (!prepare_run(repaints, windows)) {
        return std::deque<Toplevel*>();
    }

    auto const ftrace_identifier = QString::fromStdString("paint-" + std::to_string(index));

    Perf::Ftrace::begin(ftrace_identifier, ++msc);

    auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

    // Start the actual painting process.
    auto const duration = compositor->scene()->paint(base, repaints, windows, now);

    update_paint_periods(duration);
    retard_next_run();

    if (!windows.empty()) {
        compositor->presentation->lock(this, windows);
    }

    Perf::Ftrace::end(ftrace_identifier, msc);

    return windows;
}

void output::swapped_sw()
{
    compositor->presentation->softwarePresented(Presentation::Kind::Vsync);
    swapped();
}

void output::swapped_hw(unsigned int sec, unsigned int usec)
{
    auto const flags = Presentation::Kind::Vsync | Presentation::Kind::HwClock
        | Presentation::Kind::HwCompletion;
    compositor->presentation->presented(this, sec, usec, flags);
    swapped();
}

void output::swapped()
{
    if (!swap_pending) {
        qCWarning(KWIN_CORE) << "render::wayland::output::swapped called but no swap pending.";
        return;
    }
    swap_pending = false;

    // We delay the next paint shortly before next vblank. For that we assume that the swap
    // event is close to the actual vblank (TODO: it would be better to take the actual flip
    // time that for example DRM events provide). We take 10% of refresh cycle length.
    // We also assume the paint duration is relatively constant over time. We take 3 times the
    // previous paint duration.
    //
    // All temporary calculations are in nanoseconds but the final timer offset in the end in
    // milliseconds. Atleast we take here one millisecond.
    auto const refresh = refresh_length();
    auto const vblankMargin = refresh / 10;

    auto max_paint_duration = [this]() {
        if (last_paint_durations[0] > last_paint_durations[1]) {
            return last_paint_durations[0];
        }
        return last_paint_durations[1];
    };

    auto const paint_margin = max_paint_duration();
    delay = std::max(refresh - vblankMargin - paint_margin, int64_t(0));

    delay_timer.stop();
    set_delay_timer();
}

int64_t output::refresh_length() const
{
    return 1000 * 1000 / base->refreshRate();
}

void output::update_paint_periods(int64_t duration)
{
    if (duration > last_paint_durations[1]) {
        last_paint_durations[1] = duration;
    }

    paint_periods++;

    // We take the maximum over the last 100 frames.
    if (paint_periods == 100) {
        last_paint_durations[0] = last_paint_durations[1];
        last_paint_durations[1] = 0;
        paint_periods = 0;
    }
}

void output::set_delay_timer()
{
    if (delay_timer.isActive() || swap_pending) {
        // Abort since we will composite when the timer runs out or the timer will only get
        // started at buffer swap.
        return;
    }

    // In milliseconds.
    uint const wait_time = delay / 1000 / 1000;

    auto const ftrace_identifier = QString::fromStdString("timer-" + std::to_string(index));
    Perf::Ftrace::mark(ftrace_identifier + QString::number(wait_time));

    // Force 4fps minimum:
    delay_timer.start(std::min(wait_time, 250u), this);
}

void output::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == delay_timer.timerId()) {
        run();
        return;
    }
    QObject::timerEvent(event);
}

void output::retard_next_run()
{
    if (compositor->scene()->hasSwapEvent()) {
        // We wait on an explicit callback from the backend to unlock next composition runs.
        return;
    }
    delay = refresh_length();
    set_delay_timer();
}

}
