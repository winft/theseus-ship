/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "compositor.h"
#include "effects.h"
#include "utils.h"

#include "base/wayland/output.h"
#include "base/wayland/platform.h"
#include "base/wayland/server.h"
#include "debug/perf/ftrace.h"
#include "render/gl/scene.h"
#include "render/platform.h"
#include "toplevel.h"
#include "wayland_logging.h"
#include "win/remnant.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/x11/stacking_tree.h"

#include <kwinglplatform.h>
#include <kwingltexture.h>

#include <Wrapland/Server/surface.h>

namespace KWin::render::wayland
{

static int s_index{0};

static compositor* get_compositor(render::platform& platform)
{
    return static_cast<wayland::compositor*>(platform.compositor.get());
}

output::output(base::wayland::output& base, render::platform& platform)
    : index{++s_index}
    , platform{platform}
    , base{base}
{
}

void output::add_repaint(QRegion const& region)
{
    auto const capped_region = region.intersected(base.geometry());
    if (capped_region.isEmpty()) {
        return;
    }
    repaints_region += capped_region;
    set_delay_timer();
}

bool output::prepare_repaint(Toplevel* win)
{
    if (!win->has_pending_repaints()) {
        return false;
    }

    auto const repaints = win->repaints();
    if (repaints.intersected(base.geometry()).isEmpty()) {
        // TODO(romangg): Remove win from windows list?
        return false;
    }

    for (auto& output : static_cast<base::wayland::platform&>(platform.base).outputs) {
        if (output == &base) {
            continue;
        }
        auto const capped_region = repaints.intersected(output->geometry());
        if (!capped_region.isEmpty()) {
            output->render->add_repaint(capped_region);
        }
    }

    return true;
}

bool output::prepare_run(QRegion& repaints, std::deque<Toplevel*>& windows)
{
    delay_timer.stop();
    frame_timer.stop();

    // If a buffer swap is still pending, we return to the event loop and
    // continue processing events until the swap has completed.
    if (swap_pending) {
        return false;
    }
    if (get_compositor(platform)->is_locked()) {
        return false;
    }

    // Create a list of all windows in the stacking order
    windows = workspace()->x_stacking_tree->as_list();
    bool has_window_repaints{false};
    std::deque<Toplevel*> frame_windows;

    auto window_it = windows.begin();
    while (window_it != windows.end()) {
        auto win = *window_it;

        if (win->remnant() && win->transient()->annexed) {
            if (auto lead = win::lead_of_annexed_transient(win); !lead || !lead->remnant()) {
                // TODO(romangg): Add repaint to compositor?
                win->remnant()->refcount = 0;
                delete win;
                window_it = windows.erase(window_it);
                continue;
            }
        }
        window_it++;

        if (prepare_repaint(win)) {
            has_window_repaints = true;
        } else if (win->surface()
                   && win->surface()->client() != waylandServer()->xwayland_connection()
                   && (win->surface()->state().updates & Wrapland::Server::surface_change::frame)
                   && max_coverage_output(win) == &base) {
            frame_windows.push_back(win);
        }
        if (win->resetAndFetchDamage()) {
            // Discard the cached lanczos texture
            if (win->transient()->annexed) {
                win = win::lead_of_annexed_transient(win);
            }
            assert(win->render);
            assert(win->render->effect);
            auto const texture = win->render->effect->data(LanczosCacheRole);
            if (texture.isValid()) {
                delete static_cast<GLTexture*>(texture.value<void*>());
                win->render->effect->setData(LanczosCacheRole, QVariant());
            }
        }
    }

    // Move elevated windows to the top of the stacking order
    for (auto effect_window : static_cast<effects_handler_impl*>(effects)->elevatedWindows()) {
        auto window = static_cast<effects_window_impl*>(effect_window)->window();
        remove_all(windows, window);
        windows.push_back(window);
    }

    if (repaints_region.isEmpty() && !has_window_repaints) {
        idle = true;
        get_compositor(platform)->check_idle();

        // This means the next time we composite it is done without timer delay.
        delay = std::chrono::nanoseconds::zero();

        if (!frame_windows.empty()) {
            // Some windows want a frame event still.
            get_compositor(platform)->presentation->frame(this, frame_windows);
        }
        return false;
    }

    idle = false;

    // Skip windows that are not yet ready for being painted and if screen is locked skip windows
    // that are neither lockscreen nor inputmethod windows.
    //
    // TODO? This cannot be used so carelessly - needs protections against broken clients, the
    // window should not get focus before it's displayed, handle unredirected windows properly and
    // so on.
    remove_all_if(windows, [](auto& win) {
        auto screen_lock_filtered
            = kwinApp()->is_screen_locked() && !win->isLockScreen() && !win->isInputMethod();

        return !win->readyForPainting() || screen_lock_filtered;
    });

    // Submit pending output repaints and clear the pending field, so that post-pass can add new
    // repaints for the next repaint.
    repaints = repaints_region;
    repaints_region = QRegion();

    return true;
}

#define SWAP_TIME_DEBUG 0
#if SWAP_TIME_DEBUG
auto to_ms = [](std::chrono::nanoseconds val) {
    QString ret = QString::number(std::chrono::duration<double, std::milli>(val).count()) + "ms";
    return ret;
};
#endif

std::deque<Toplevel*> output::run()
{
    QRegion repaints;
    std::deque<Toplevel*> windows;

    QElapsedTimer test_timer;
    test_timer.start();

    if (!prepare_run(repaints, windows)) {
        return std::deque<Toplevel*>();
    }

    auto const ftrace_identifier = QString::fromStdString("paint-" + std::to_string(index));

    Perf::Ftrace::begin(ftrace_identifier, ++msc);

    auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

    // Start the actual painting process.
    auto const duration = std::chrono::nanoseconds(
        get_compositor(platform)->scene()->paint_output(&base, repaints, windows, now));

#if SWAP_TIME_DEBUG
    qDebug().noquote() << "RUN gap:" << to_ms(now_ns - swap_ref_time)
                       << "paint:" << to_ms(duration);
    swap_ref_time = now_ns;
#endif

    paint_durations.update(duration);
    retard_next_run();

    if (!windows.empty()) {
        get_compositor(platform)->presentation->lock(this, windows);
    }

    Perf::Ftrace::end(ftrace_identifier, msc);

    return windows;
}

void output::dry_run()
{
    auto windows = workspace()->x_stacking_tree->as_list();
    std::deque<Toplevel*> frame_windows;

    for (auto win : windows) {
        if (!win->surface() || win->surface()->client() == waylandServer()->xwayland_connection()) {
            continue;
        }
        if (!(win->surface()->state().updates & Wrapland::Server::surface_change::frame)) {
            continue;
        }
        frame_windows.push_back(win);
    }
    get_compositor(platform)->presentation->frame(this, frame_windows);
}

void output::presented(presentation_data const& data)
{
    get_compositor(platform)->presentation->presented(this, data);
    last_presentation = data;
}

void output::frame()
{
    get_compositor(platform)->presentation->presented(this, last_presentation);

    if (!swap_pending) {
        qCWarning(KWIN_WL) << "render::wayland::output::presented called but no swap pending.";
        return;
    }
    swap_pending = false;

    set_delay(last_presentation);
    delay_timer.stop();
    set_delay_timer();
}

std::chrono::nanoseconds output::refresh_length() const
{
    return std::chrono::nanoseconds(1000 * 1000 * (1000 * 1000 / base.refresh_rate()));
}

void output::set_delay(presentation_data const& data)
{
    auto scene = platform.compositor->scene();
    if (scene->compositingType() != CompositingType::OpenGLCompositing) {
        return;
    }
    if (!GLPlatform::instance()->supports(GLFeature::TimerQuery)) {
        return;
    }

    static_cast<gl::scene*>(scene)->backend()->makeCurrent();

#if SWAP_TIME_DEBUG
    qDebug() << "";
    std::chrono::nanoseconds render_time_debug;
#endif

    // First get the latest Gl timer queries.
    last_timer_queries.erase(std::remove_if(last_timer_queries.begin(),
                                            last_timer_queries.end(),
#if SWAP_TIME_DEBUG
                                            [this, &render_time_debug](auto& timer) {
#else
                                            [this](auto& timer) {
#endif
                                                if (!timer.get_query()) {
                                                    return false;
                                                }
#if SWAP_TIME_DEBUG
                                                render_time_debug = timer.time();
#endif
                                                render_durations.update(timer.time());
                                                return true;
                                            }),
                             last_timer_queries.end());

    auto now = std::chrono::steady_clock::now().time_since_epoch();

    // The gap between the last presentation on the display and us now calculating the delay.
    auto vblank_to_now = now - data.when;

    // The refresh cycle length either from the presentation data, or if not available, our guess.
    auto const refresh
        = data.refresh > std::chrono::nanoseconds::zero() ? data.refresh : refresh_length();

    // Some relative gap to factor in the unknown time the hardware needs to put a rendered image
    // onto the scanout buffer.
    auto const hw_margin = refresh / 10;

    // We try to delay the next paint shortly before next vblank factoring in our margins.
    auto try_delay = refresh - vblank_to_now - hw_margin - paint_durations.get_max()
        - render_durations.get_max();

    // If our previous margins were too large we don't delay. We would likely miss the next vblank.
    delay = std::max(try_delay, std::chrono::nanoseconds::zero());

#if SWAP_TIME_DEBUG
    QDebug debug = qDebug();
    debug.noquote().nospace();
    debug << "SWAP total: " << to_ms((now - swap_ref_time)) << endl;
    debug << "vblank to now: " << to_ms(now) << " - " << to_ms(data.when) << " = "
          << to_ms(vblank_to_now) << endl;
    debug << "MARGINS vblank: " << to_ms(hw_margin)
          << " paint: " << to_ms(paint_durations.get_max())
          << " render: " << to_ms(render_time_debug) << "(" << to_ms(render_durations.get_max())
          << ")" << endl;
    debug << "refresh: " << to_ms(refresh) << " delay: " << to_ms(try_delay) << " (" << to_ms(delay)
          << ")";
    swap_ref_time = now;
#endif
}

bool waiting_for_event(output const& out)
{
    return out.delay_timer.isActive() || out.swap_pending || !out.base.is_dpms_on();
}

void output::set_delay_timer()
{
    if (waiting_for_event(*this)) {
        // Abort since we will composite when the timer runs out or the timer will only get
        // started at buffer swap.
        return;
    }

    // In milliseconds.
    auto const wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(delay);

    auto const ftrace_identifier = QString::fromStdString("timer-" + std::to_string(index));
    Perf::Ftrace::mark(ftrace_identifier + QString::number(wait_time.count()));

    // Force 4fps minimum:
    delay_timer.start(std::min(wait_time, std::chrono::milliseconds(250)).count(), this);
}

void output::request_frame(Toplevel* window)
{
    if (waiting_for_event(*this) || frame_timer.isActive()) {
        // Frame will be received when timer runs out.
        return;
    }

    get_compositor(platform)->presentation->frame(this, {window});
    frame_timer.start(
        std::chrono::duration_cast<std::chrono::milliseconds>(refresh_length()).count(), this);
}

void output::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == delay_timer.timerId()) {
        run();
        return;
    }
    if (event->timerId() == frame_timer.timerId()) {
        dry_run();
        return;
    }
    QObject::timerEvent(event);
}

void output::retard_next_run()
{
    if (get_compositor(platform)->scene()->hasSwapEvent()) {
        // We wait on an explicit callback from the backend to unlock next composition runs.
        return;
    }
    delay = refresh_length();
    set_delay_timer();
}

}
