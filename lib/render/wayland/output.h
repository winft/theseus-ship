/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "duration_record.h"
#include "presentation.h"

#include "base/logging.h"
#include "base/seat/session.h"
#include "debug/perf/ftrace.h"
#include "render/gl/scene.h"
#include "render/gl/timer_query.h"
#include "win/remnant.h"
#include "win/space_window_release.h"

#include <render/gl/interface/platform.h>

#include <QBasicTimer>
#include <QRegion>
#include <QTimer>
#include <Wrapland/Server/surface.h>
#include <chrono>
#include <deque>
#include <map>
#include <vector>

namespace KWin::render::wayland
{

#define SWAP_TIME_DEBUG 0
#if SWAP_TIME_DEBUG
auto to_ms = [](std::chrono::nanoseconds val) {
    QString ret = QString::number(std::chrono::duration<double, std::milli>(val).count()) + "ms";
    return ret;
};
#endif

template<typename Output>
bool output_waiting_for_event(Output const& out)
{
    return out.delay_timer.isActive() || out.swap_pending || !out.base.is_dpms_on()
        || !out.platform.base.session->isActiveSession();
}

template<typename Base, typename Platform>
class output : public QObject
{
public:
    using space_t = typename Platform::space_t;
    using window_t = typename Platform::compositor_t::window_t;

    output(Base& base, Platform& platform)
        : platform{platform}
        , base{base}
        , index{++platform.output_index}
    {
    }

    virtual void reset()
    {
        if (platform.compositor) {
            platform.compositor->addRepaint(base.geometry());
        }
    }

    void disable()
    {
        delay_timer.stop();
        frame_timer.stop();
    }
    void add_repaint(QRegion const& region)
    {
        auto const capped_region = region.intersected(base.geometry());
        if (capped_region.isEmpty()) {
            return;
        }
        repaints_region += capped_region;
        set_delay_timer();
    }

    void set_delay(presentation_data const& data)
    {
        auto& scene = platform.compositor->scene;
        if (!scene->isOpenGl()) {
            return;
        }
        if (!GLPlatform::instance()->supports(GLFeature::TimerQuery)) {
            return;
        }

        static_cast<gl::scene<typename Platform::compositor_t>&>(*scene).backend()->makeCurrent();

        // First get the latest Gl timer queries.
        std::chrono::nanoseconds render_time_debug;
        last_timer_queries.erase(std::remove_if(last_timer_queries.begin(),
                                                last_timer_queries.end(),
                                                [this, &render_time_debug](auto& timer) {
                                                    if (!timer.get_query()) {
                                                        return false;
                                                    }
                                                    render_time_debug = timer.time();
                                                    render_durations.update(timer.time());
                                                    return true;
                                                }),
                                 last_timer_queries.end());

        auto now = std::chrono::steady_clock::now().time_since_epoch();

        // The gap between the last presentation on the display and us now calculating the delay.
        auto vblank_to_now = now - data.when;

        // The refresh cycle length either from the presentation data, or if not available, our
        // guess.
        auto const refresh
            = data.refresh > std::chrono::nanoseconds::zero() ? data.refresh : refresh_length();

        // Some relative gap to factor in the unknown time the hardware needs to put a rendered
        // image onto the scanout buffer.
        auto const hw_margin = refresh / 10;

        // We try to delay the next paint shortly before next vblank factoring in our margins.
        auto try_delay = refresh - vblank_to_now - hw_margin - paint_durations.get_max()
            - render_durations.get_max();

        // If our previous margins were too large we don't delay. We would likely miss the next
        // vblank.
        delay = std::max(try_delay, std::chrono::nanoseconds::zero());

#if SWAP_TIME_DEBUG
        QDebug debug = qDebug();
        debug.noquote().nospace();
        debug << "\nSWAP total: " << to_ms((now - swap_ref_time)) << endl;
        debug << "vblank to now: " << to_ms(now) << " - " << to_ms(data.when) << " = "
              << to_ms(vblank_to_now) << endl;
        debug << "MARGINS vblank: " << to_ms(hw_margin)
              << " paint: " << to_ms(paint_durations.get_max())
              << " render: " << to_ms(render_time_debug) << "(" << to_ms(render_durations.get_max())
              << ")" << endl;
        debug << "refresh: " << to_ms(refresh) << " delay: " << to_ms(try_delay) << " ("
              << to_ms(delay) << ")";
        swap_ref_time = now;
#endif
    }

    void set_delay_timer()
    {
        if (output_waiting_for_event(*this)) {
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

    template<typename Win>
    void request_frame(Win* window)
    {
        using var_win = typename Win::space_t::window_t;

        if (output_waiting_for_event(*this) || frame_timer.isActive()) {
            // Frame will be received when timer runs out.
            return;
        }

        platform.compositor->presentation->template frame<var_win>(this, {var_win(window)});
        frame_timer.start(
            std::chrono::duration_cast<std::chrono::milliseconds>(refresh_length()).count(), this);
    }

    void run()
    {
        QRegion repaints;
        std::deque<typename space_t::window_t> windows;

        QElapsedTimer test_timer;
        test_timer.start();

        if (!prepare_run(repaints, windows)) {
            return;
        }

        auto const ftrace_identifier = QString::fromStdString("paint-" + std::to_string(index));

        Perf::Ftrace::begin(ftrace_identifier, ++msc);

        auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

        // Start the actual painting process.
        auto const duration = std::chrono::nanoseconds(
            platform.compositor->scene->paint_output(&base, repaints, windows, now));

#if SWAP_TIME_DEBUG
        qDebug().noquote() << "RUN gap:" << to_ms(now_ns - swap_ref_time)
                           << "paint:" << to_ms(duration);
        swap_ref_time = now_ns;
#endif

        paint_durations.update(duration);
        retard_next_run();

        if (!windows.empty()) {
            platform.compositor->presentation->lock(this, windows);
        }

        for (auto win : windows) {
            std::visit(overload{[&](auto&& win) {
                           if (win->remnant && !win->remnant->refcount) {
                               win::delete_window_from_space(win->space, *win);
                           }
                       }},
                       win);
        }

        Perf::Ftrace::end(ftrace_identifier, msc);
    }

    void dry_run()
    {
        auto windows = win::render_stack(platform.compositor->space->stacking.order);
        std::deque<typename space_t::window_t> frame_windows;

        for (auto win : windows) {
            std::visit(overload{[&](auto&& win) {
                           if constexpr (requires(decltype(win) win) { win->surface; }) {
                               if (!win->surface
                                   || win->surface->client()
                                       == platform.base.server->xwayland_connection()) {
                                   return;
                               }
                               if (!(win->surface->state().updates
                                     & Wrapland::Server::surface_change::frame)) {
                                   return;
                               }
                               frame_windows.push_back(win);
                           }
                       }},
                       win);
        }
        platform.compositor->presentation->frame(this, frame_windows);
    }

    void presented(presentation_data const& data)
    {
        platform.compositor->presentation->presented(this, data);
        last_presentation = data;
    }

    void frame()
    {
        platform.compositor->presentation->presented(this, last_presentation);

        if (!swap_pending) {
            qCWarning(KWIN_CORE)
                << "render::wayland::output::presented called but no swap pending.";
            return;
        }
        swap_pending = false;

        set_delay(last_presentation);
        delay_timer.stop();
        set_delay_timer();
    }

    Platform& platform;
    Base& base;

    std::map<uint32_t, Wrapland::Server::Surface*> assigned_surfaces;

    bool idle{true};
    bool swap_pending{false};
    QBasicTimer delay_timer;
    QBasicTimer frame_timer;
    std::vector<render::gl::timer_query> last_timer_queries;

private:
    template<typename Win>
    bool prepare_repaint(Win* win)
    {
        if (!win->has_pending_repaints()) {
            return false;
        }

        auto const repaints = win::repaints(*win);
        if (repaints.intersected(base.geometry()).isEmpty()) {
            // TODO(romangg): Remove win from windows list?
            return false;
        }

        for (auto& output : platform.base.outputs) {
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

    bool prepare_run(QRegion& repaints, std::deque<typename space_t::window_t>& windows)
    {
        delay_timer.stop();
        frame_timer.stop();

        // If a buffer swap is still pending, we return to the event loop and
        // continue processing events until the swap has completed.
        if (swap_pending) {
            return false;
        }
        if (platform.compositor->is_locked()) {
            return false;
        }

        // Create a list of all windows in the stacking order
        windows = win::render_stack(platform.compositor->space->stacking.order);
        bool has_window_repaints{false};
        std::deque<typename space_t::window_t> frame_windows;

        auto window_it = windows.begin();
        while (window_it != windows.end()) {
            std::visit(overload{[&](auto&& win) {
                           if (win->remnant && win->transient->annexed) {
                               if (auto lead = win::lead_of_annexed_transient(win);
                                   !lead || !lead->remnant) {
                                   // TODO(romangg): Add repaint to compositor?
                                   win->remnant->refcount = 0;
                                   win::delete_window_from_space(win->space, *win);
                                   window_it = windows.erase(window_it);
                                   return;
                               }
                           }

                           window_it++;

                           if (prepare_repaint(win)) {
                               has_window_repaints = true;
                           } else {
                               if constexpr (requires(decltype(win) win) { win->surface; }) {
                                   if (win->surface
                                       && win->surface->client()
                                           != platform.base.server->xwayland_connection()
                                       && (win->surface->state().updates
                                           & Wrapland::Server::surface_change::frame)
                                       && max_coverage_output(win) == &base) {
                                       frame_windows.push_back(win);
                                   }
                               }
                           }

                           if (win->render_data.is_damaged) {
                               assert(win->render);
                               assert(win->render->effect);

                               win->render_data.is_damaged = false;

                               // Discard the cached lanczos texture
                               if (win->transient->annexed) {
                                   win = win::lead_of_annexed_transient(win);
                               }

                               auto const texture = win->render->effect->data(LanczosCacheRole);
                               if (texture.isValid()) {
                                   delete static_cast<GLTexture*>(texture.template value<void*>());
                                   win->render->effect->setData(LanczosCacheRole, QVariant());
                               }
                           }
                       }},
                       *window_it);
        }

        // Move elevated windows to the top of the stacking order
        auto const elevated_windows = platform.compositor->effects->elevatedWindows();
        for (auto effect_window : elevated_windows) {
            auto window
                = static_cast<effects_window_impl<window_t>*>(effect_window)->window.ref_win;
            if (!move_to_back(windows, *window)) {
                windows.push_back(*window);
            }
        }

        if (repaints_region.isEmpty() && !has_window_repaints) {
            idle = true;
            platform.compositor->check_idle();

            // This means the next time we composite it is done without timer delay.
            delay = std::chrono::nanoseconds::zero();

            if (!frame_windows.empty()) {
                // Some windows want a frame event still.
                platform.compositor->presentation->frame(this, frame_windows);
            }
            return false;
        }

        idle = false;
        auto const screen_lock_filtered = base::wayland::is_screen_locked(platform.base);

        // Skip windows that are not yet ready for being painted and if screen is locked skip
        // windows that are neither lockscreen nor inputmethod windows.
        //
        // TODO? This cannot be used so carelessly - needs protections against broken clients, the
        // window should not get focus before it's displayed, handle unredirected windows properly
        // and so on.
        remove_all_if(windows, [screen_lock_filtered](auto& win) {
            return std::visit(
                overload{[&](auto&& win) {
                    auto filtered = screen_lock_filtered;
                    if (filtered) {
                        if constexpr (requires(decltype(win) win) { win->isLockScreen(); }) {
                            filtered &= !win->isLockScreen();
                        }
                        if constexpr (requires(decltype(win) win) { win->isInputMethod(); }) {
                            filtered &= !win->isInputMethod();
                        }
                    }

                    return !win->render_data.ready_for_painting || filtered;
                }},
                win);
        });

        // Submit pending output repaints and clear the pending field, so that post-pass can add new
        // repaints for the next repaint.
        repaints = repaints_region;
        repaints_region = QRegion();

        return true;
    }

    void retard_next_run()
    {
        if (platform.compositor->scene->hasSwapEvent()) {
            // We wait on an explicit callback from the backend to unlock next composition runs.
            return;
        }
        delay = refresh_length();
        set_delay_timer();
    }

    std::chrono::nanoseconds refresh_length() const
    {
        return std::chrono::nanoseconds(1000 * 1000 * (1000 * 1000 / base.refresh_rate()));
    }

    void timerEvent(QTimerEvent* event) override
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

    int index;

    ulong msc{0};

    // Compositing delay.
    std::chrono::nanoseconds delay{0};

    presentation_data last_presentation;
    duration_record paint_durations;
    duration_record render_durations;

    // Used for debugging rendering time.
    std::chrono::nanoseconds swap_ref_time{};

    QRegion repaints_region;
};

}
