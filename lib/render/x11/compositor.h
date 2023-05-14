/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor_selection_owner.h"
#include "effects.h"
#include "overlay_window.h"
#include "shadow.h"
#include "types.h"

#include "debug/perf/ftrace.h"
#include "render/compositor.h"
#include "render/compositor_start.h"
#include "render/dbus/compositing.h"
#include "render/effect/window_impl.h"
#include "render/gl/scene.h"
#include "render/platform.h"
#include "render/support_properties.h"
#include "win/remnant.h"
#include "win/space_window_release.h"
#include "win/stacking_order.h"

#include <KNotification>
#include <QAction>
#include <QObject>
#include <QRegion>
#include <QTimer>
#include <deque>
#include <memory>

namespace KWin::render::x11
{

template<typename Compositor, typename Factory>
std::unique_ptr<render::scene<typename Compositor::platform_t>>
create_scene_impl(Compositor& compositor, Factory& factory, std::string const& prev_err)
{
    auto setup_hooks = [&](auto& scene) {
        scene->windowing_integration.handle_viewport_limits_alarm = [&] {
            qCDebug(KWIN_CORE) << "Suspending compositing because viewport limits are not met";
            QTimer::singleShot(
                0, compositor.qobject.get(), [&] { compositor.suspend(suspend_reason::all); });
        };
    };

    try {
        auto scene = factory(compositor.platform);
        setup_hooks(scene);
        if (!prev_err.empty()) {
            qCDebug(KWIN_CORE) << "Fallback after error:" << prev_err.c_str();
        }
        return scene;
    } catch (std::runtime_error const& exc) {
        throw std::runtime_error(prev_err + " " + exc.what());
    }
}

template<typename Platform>
class compositor
{
public:
    using qobject_t = compositor_qobject;
    using platform_t = Platform;
    using type = compositor<Platform>;
    using scene_t = render::scene<Platform>;
    using effects_t = x11::effects_handler_impl<scene_t>;
    using overlay_window_t = x11::overlay_window<type>;
    using space_t = typename Platform::base_t::space_t;
    using x11_ref_window_t = typename space_t::x11_window;
    using window_t = render::window<typename space_t::window_t, type>;
    using effect_window_t = typename window_t::effect_window_t;
    using state_t = render::state;
    using shadow_t = render::shadow<window_t>;

    compositor(Platform& platform)
        : qobject{std::make_unique<compositor_qobject>(
            [this](auto te) { return handle_timer_event(te); })}
        , platform{platform}
        , m_suspended(platform.options->qobject->isUseCompositing() ? suspend_reason::none
                                                                    : suspend_reason::user)
        , dbus{std::make_unique<dbus::compositing<type>>(*this)}
    {
        compositor_setup(*this);

        this->dbus->qobject->integration.get_types = [] { return QStringList{"glx"}; };
        this->dbus->qobject->integration.resume = [this] { resume(suspend_reason::script); };
        this->dbus->qobject->integration.suspend = [this] { suspend(suspend_reason::script); };

        if (qEnvironmentVariableIsSet("KWIN_MAX_FRAMES_TESTED")) {
            m_framesToTestForSafety = qEnvironmentVariableIntValue("KWIN_MAX_FRAMES_TESTED");
        }

        m_releaseSelectionTimer.setSingleShot(true);
        m_releaseSelectionTimer.setInterval(compositor_lost_message_delay);
        QObject::connect(&m_releaseSelectionTimer, &QTimer::timeout, this->qobject.get(), [this] {
            releaseCompositorSelection();
        });
        QObject::connect(this->qobject.get(),
                         &compositor_qobject::aboutToToggleCompositing,
                         this->qobject.get(),
                         [this] { overlay_window = nullptr; });
    }

    ~compositor()
    {
        Q_EMIT this->qobject->aboutToDestroy();
        compositor_stop(*this, true);
        delete_unused_support_properties(*this);
        compositor_destroy_selection(*this);
    }

    void start(space_t& space)
    {
        if (!this->space) {
            // On first start setup connections.
            QObject::connect(&space.base, &base::platform::x11_reset, this->qobject.get(), [this] {
                compositor_setup_x11_support(*this);
            });
            QObject::connect(space.stacking.order.qobject.get(),
                             &win::stacking_order_qobject::changed,
                             this->qobject.get(),
                             [this] { full_repaint(*this); });
            QObject::connect(space.qobject.get(),
                             &space_t::qobject_t::currentDesktopChanged,
                             this->qobject.get(),
                             [this] { full_repaint(*this); });
            this->space = &space;
        }

        if (flags(m_suspended)) {
            QStringList reasons;
            if (flags(m_suspended & suspend_reason::user)) {
                reasons << QStringLiteral("Disabled by User");
            }
            if (flags(m_suspended & suspend_reason::rule)) {
                reasons << QStringLiteral("Disabled by Window");
            }
            if (flags(m_suspended & suspend_reason::script)) {
                reasons << QStringLiteral("Disabled by Script");
            }
            qCDebug(KWIN_CORE) << "Compositing is suspended, reason:" << reasons;
            return;
        }

        if (!this->platform.compositingPossible()) {
            qCCritical(KWIN_CORE) << "Compositing is not possible";
            return;
        }

        try {
            compositor_start_scene(*this);
        } catch (std::runtime_error const& ex) {
            qCWarning(KWIN_CORE) << "Error: " << ex.what();
            qCWarning(KWIN_CORE) << "Compositing not possible. Continue without it.";

            state = state::off;
            xcb_composite_unredirect_subwindows(space.base.x11_data.connection,
                                                space.base.x11_data.root_window,
                                                XCB_COMPOSITE_REDIRECT_MANUAL);
            compositor_destroy_selection(*this);
        }
    }

    void schedule_repaint()
    {
        if (state == state::on) {
            setCompositeTimer();
        }
    }

    template<typename Win>
    void schedule_repaint(Win* /*window*/)
    {
        schedule_repaint();
    }

    bool handle_timer_event(QTimerEvent* te)
    {
        if (te->timerId() != compositeTimer.timerId()) {
            return false;
        }
        performCompositing();
        return true;
    }

    /**
     * Notifies the compositor that SwapBuffers() is about to be called.
     * Rendering of the next frame will be deferred until bufferSwapComplete()
     * is called.
     */
    void aboutToSwapBuffers()
    {
        assert(!this->m_bufferSwapPending);
        this->m_bufferSwapPending = true;
    }

    /**
     * Notifies the compositor that a pending buffer swap has completed.
     */
    void bufferSwapComplete(bool present = true)
    {
        Q_UNUSED(present)

        if (!m_bufferSwapPending) {
            qDebug()
                << "KWin::Compositor::bufferSwapComplete() called but m_bufferSwapPending is false";
            return;
        }
        m_bufferSwapPending = false;

        // We delay the next paint shortly before next vblank. For that we assume that the swap
        // event is close to the actual vblank (TODO: it would be better to take the actual flip
        // time that for example DRM events provide). We take 10% of refresh cycle length.
        // We also assume the paint duration is relatively constant over time. We take 3 times the
        // previous paint duration.
        //
        // All temporary calculations are in nanoseconds but the final timer offset in the end in
        // milliseconds. Atleast we take here one millisecond.
        const qint64 refresh = refreshLength();
        const qint64 vblankMargin = refresh / 10;

        auto maxPaintDuration = [this]() {
            if (m_lastPaintDurations[0] > m_lastPaintDurations[1]) {
                return m_lastPaintDurations[0];
            }
            return m_lastPaintDurations[1];
        };
        auto const paintMargin = maxPaintDuration();
        m_delay = qMax(refresh - vblankMargin - paintMargin, qint64(0));

        compositeTimer.stop();
        setCompositeTimer();
    }

    void toggleCompositing()
    {
        if (flags(m_suspended)) {
            // Direct user call; clear all bits.
            resume(suspend_reason::all);
        } else {
            // But only set the user one (sufficient to suspend).
            suspend(suspend_reason::user);
        }
    }

    /**
     * @brief Suspends the Compositor if it is currently active.
     *
     * Note: it is possible that the Compositor is not able to suspend. Read state to check
     * whether the Compositor has been suspended.
     */
    void suspend(suspend_reason reason)
    {
        assert(reason != suspend_reason::none);
        m_suspended |= reason;

        if (flags(reason & suspend_reason::script)) {
            // When disabled show a shortcut how the user can get back compositing.
            auto const shortcuts = platform.base.input->shortcuts->get_keyboard_shortcut(
                this->space->qobject->template findChild<QAction*>(
                    QStringLiteral("Suspend Compositing")));
            if (!shortcuts.isEmpty()) {
                // Display notification only if there is the shortcut.
                const QString message = i18n(
                    "Desktop effects have been suspended by another application.<br/>"
                    "You can resume using the '%1' shortcut.",
                    shortcuts.first().toString(QKeySequence::NativeText));
                KNotification::event(QStringLiteral("compositingsuspendeddbus"), message);
            }
        }
        m_releaseSelectionTimer.start();
        compositor_stop(*this, false);
    }

    /**
     * @brief Resumes the Compositor if it is currently suspended.
     *
     * Note: it is possible that the Compositor cannot be resumed, that is there might be Clients
     * blocking the usage of Compositing or the Scene might be broken. Read state to check
     * whether the Compositor has been resumed. Also check isCompositingPossible and
     * isOpenGLBroken.
     *
     * Note: The starting of the Compositor can require some time and is partially done threaded.
     * After this method returns the setup may not have been completed.
     *
     * @see suspend
     * @see isCompositingPossible
     * @see isOpenGLBroken
     */
    void resume(suspend_reason reason)
    {
        assert(reason != suspend_reason::none);
        m_suspended &= ~reason;

        assert(this->space);
        start(*this->space);
    }

    void reinitialize()
    {
        // Resume compositing if suspended.
        m_suspended = suspend_reason::none;
        // TODO(romangg): start the release selection timer?
        reinitialize_compositor(*this);
    }

    void addRepaint(QRegion const& region)
    {
        if (state != state::on) {
            return;
        }
        this->repaints_region += region;
        schedule_repaint();
    }

    void configChanged()
    {
        if (flags(m_suspended)) {
            // TODO(romangg): start the release selection timer?
            compositor_stop(*this, false);
            return;
        }
        reinitialize();
        full_repaint(*this);
    }

    /**
     * Checks whether @p w is the Scene's overlay window.
     */
    bool is_overlay_window(WId w) const
    {
        if (!overlay_window) {
            // No overlay window, it cannot be the overlay.
            return false;
        }
        // Compare the window ID's.
        return w == overlay_window->window();
    }

    template<typename Win>
    void update_blocking(Win* window)
    {
        if (window) {
            if (window->isBlockingCompositing()) {
                // Do NOT attempt to call suspend(true) from within the eventchain!
                if (!(m_suspended & suspend_reason::rule))
                    QMetaObject::invokeMethod(
                        this->qobject.get(),
                        [this]() { suspend(suspend_reason::rule); },
                        Qt::QueuedConnection);
            }
        } else if (flags(m_suspended & suspend_reason::rule)) {
            // If !c we just check if we can resume in case a blocking client was lost.
            bool shouldResume = true;

            for (auto const& win : this->space->windows) {
                if (std::visit(overload{[&](auto&& win) { return win->isBlockingCompositing(); }},
                               win)) {
                    shouldResume = false;
                    break;
                }
            }
            if (shouldResume) {
                // Do NOT attempt to call suspend(false) from within the eventchain!
                QMetaObject::invokeMethod(
                    this->qobject.get(),
                    [this]() { resume(suspend_reason::rule); },
                    Qt::QueuedConnection);
            }
        }
    }

    /**
     * @brief The overlay window used by the backend, if any.
     */
    overlay_window_t* overlay_window{nullptr};

    std::unique_ptr<render::scene<Platform>> create_scene()
    {
        using Factory = std::function<std::unique_ptr<render::scene<Platform>>(Platform&)>;

        std::deque<Factory> factories;
        factories.push_back(gl::create_scene<Platform>);

        try {
            return create_scene_impl(*this, factories.at(0), "");
        } catch (std::runtime_error const& exc) {
            if (factories.size() > 1) {
                return create_scene_impl(*this, factories.at(1), exc.what());
            }
            throw exc;
        }
    }

    template<typename RefWin>
    void integrate_shadow(RefWin& ref_win)
    {
        auto& atoms = ref_win.space.atoms;

        ref_win.render->shadow_windowing.create = [&](auto&& render_win) {
            return create_shadow<shadow_t, window_t>(render_win, atoms->kde_net_wm_shadow);
        };
        ref_win.render->shadow_windowing.update = [&](auto&& shadow) {
            return read_and_update_shadow<shadow_t>(
                shadow, ref_win.space.base.x11_data.connection, atoms->kde_net_wm_shadow);
        };
    }

    void performCompositing()
    {
        QRegion repaints;
        std::deque<typename space_t::window_t> windows;

        if (!prepare_composition(repaints, windows)) {
            return;
        }

        Perf::Ftrace::begin(QStringLiteral("Paint"), ++s_msc);
        create_opengl_safepoint(opengl_safe_point::pre_frame);

        auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

        // Start the actual painting process.
        auto const duration = this->scene->paint(repaints, windows, now);

        this->update_paint_periods(duration);
        create_opengl_safepoint(opengl_safe_point::post_frame);
        this->retard_next_composition();

        for (auto win : windows) {
            std::visit(overload{[](auto&& win) {
                           if (win->remnant && !win->remnant->refcount) {
                               win::delete_window_from_space(win->space, *win);
                           }
                       }},
                       win);
        }

        Perf::Ftrace::end(QStringLiteral("Paint"), s_msc);
    }

    std::unique_ptr<compositor_qobject> qobject;

    std::unique_ptr<scene_t> scene;
    std::unique_ptr<effects_t> effects;

    state_t state{state::off};
    x11::compositor_selection_owner* m_selectionOwner{nullptr};
    QRegion repaints_region;
    QBasicTimer compositeTimer;
    qint64 m_delay{0};
    bool m_bufferSwapPending{false};

    QList<xcb_atom_t> unused_support_properties;
    QTimer unused_support_property_timer;

    // Compositing delay (in ns).
    qint64 m_lastPaintDurations[2]{0};
    int m_paintPeriods{0};

    Platform& platform;
    space_t* space{nullptr};

private:
    int refreshRate() const
    {
        int max_refresh_rate = 60000;
        for (auto output : platform.base.outputs) {
            auto const rate = output->refresh_rate();
            if (rate > max_refresh_rate) {
                max_refresh_rate = rate;
            }
        }
        return max_refresh_rate;
    }

    /// Refresh cycle length in nanoseconds.
    qint64 refreshLength() const
    {
        return 1000 * 1000 / qint64(refreshRate());
    }

    void releaseCompositorSelection()
    {
        switch (state) {
        case state::on:
            // We are compositing at the moment. Don't release.
            break;
        case state::off:
            if (m_selectionOwner) {
                qCDebug(KWIN_CORE) << "Releasing compositor selection";
                m_selectionOwner->disown();
            }
            break;
        case state::starting:
        case state::stopping:
            // Still starting or shutting down the compositor. Starting might fail
            // or after stopping a restart might follow. So test again later on.
            m_releaseSelectionTimer.start();
            break;
        }
    }

    bool prepare_composition(QRegion& repaints, std::deque<typename space_t::window_t>& windows)
    {
        assert(windows.empty());
        this->compositeTimer.stop();

        if (overlay_window && !overlay_window->visible) {
            // Abort since nothing is visible.
            return false;
        }

        // If a buffer swap is still pending, we return to the event loop and
        // continue processing events until the swap has completed.
        if (this->m_bufferSwapPending) {
            return false;
        }

        // Create a list of all windows in the stacking order
        std::deque<typename space_t::window_t> damaged_windows;
        auto has_pending_repaints{false};

        for (auto win : win::render_stack(this->space->stacking.order)) {
            std::visit(overload{[&](x11_ref_window_t* win) {
                                    // Skip windows that are not yet ready for being painted.
                                    if (!win->render_data.ready_for_painting) {
                                        return;
                                    }

                                    has_pending_repaints |= win->has_pending_repaints();

                                    // Doesn't wait for replies.
                                    if (win::x11::damage_reset_and_fetch(*win)) {
                                        damaged_windows.push_back(win);
                                    }

                                    windows.push_back(win);
                                },
                                [&](auto&& win) {
                                    if (!win->render_data.ready_for_painting) {
                                        return;
                                    }
                                    has_pending_repaints |= win->has_pending_repaints();
                                    windows.push_back(win);
                                }},
                       win);
        }

        // If a window is damaged, trigger fence this prevents damaged windows from being composited
        // by kwin before the rendering that triggered the damage events have finished on the GPU.
        if (damaged_windows.size() > 0) {
            this->scene->triggerFence();
            if (auto c = platform.base.x11_data.connection) {
                xcb_flush(c);
            }
        }

        // Move elevated windows to the top of the stacking order
        auto const elevated_win_list = this->effects->elevatedWindows();

        for (auto c : elevated_win_list) {
            auto t = static_cast<effect_window_t*>(c)->window.ref_win;
            if (!move_to_back(windows, *t)) {
                windows.push_back(*t);
            }
        }

        auto discard_lanczos_texture = [](auto window) {
            assert(window->render);
            assert(window->render->effect);

            auto const texture = window->render->effect->data(LanczosCacheRole);
            if (texture.isValid()) {
                delete static_cast<GLTexture*>(texture.template value<void*>());
                window->render->effect->setData(LanczosCacheRole, QVariant());
            }
        };

        // Get the damage region replies if there are any damaged windows, and discard the lanczos
        // texture
        for (auto vwin : damaged_windows) {
            auto win = std::get<x11_ref_window_t*>(vwin);
            discard_lanczos_texture(win);
            win::x11::damage_fetch_region_reply(*win);
            has_pending_repaints |= win->has_pending_repaints();
        }

        // If no repaint regions got added and no window has pending repaints, return and skip this
        // paint cycle
        if (this->repaints_region.isEmpty() && !has_pending_repaints) {
            this->scene->idle();

            // This means the next time we composite it is done without timer delay.
            this->m_delay = 0;
            return false;
        }

        repaints = this->repaints_region;

        // Clear all repaints, so that post-pass can add repaints for the next repaint
        this->repaints_region = QRegion();

        return true;
    }

    void create_opengl_safepoint(opengl_safe_point safepoint)
    {
        if (m_framesToTestForSafety <= 0) {
            return;
        }
        if (!(this->scene->compositingType() & OpenGLCompositing)) {
            return;
        }

        this->platform.createOpenGLSafePoint(safepoint);

        if (safepoint == opengl_safe_point::post_frame) {
            if (--m_framesToTestForSafety == 0) {
                this->platform.createOpenGLSafePoint(opengl_safe_point::post_last_guarded_frame);
            }
        }
    }

    void retard_next_composition()
    {
        if (scene->hasSwapEvent()) {
            // We wait on an explicit callback from the backend to unlock next composition runs.
            return;
        }
        m_delay = refreshLength();
        setCompositeTimer();
    }

    void setCompositeTimer()
    {
        if (compositeTimer.isActive() || m_bufferSwapPending) {
            // Abort since we will composite when the timer runs out or the timer will only get
            // started at buffer swap.
            return;
        }

        // In milliseconds.
        const uint waitTime = m_delay / 1000 / 1000;
        Perf::Ftrace::mark(QStringLiteral("timer ") + QString::number(waitTime));

        // Force 4fps minimum:
        compositeTimer.start(qMin(waitTime, 250u), qobject.get());
    }

    void update_paint_periods(int64_t duration)
    {
        if (duration > m_lastPaintDurations[1]) {
            m_lastPaintDurations[1] = duration;
        }

        m_paintPeriods++;

        // We take the maximum over the last 100 frames.
        if (m_paintPeriods == 100) {
            m_lastPaintDurations[0] = m_lastPaintDurations[1];
            m_lastPaintDurations[1] = 0;
            m_paintPeriods = 0;
        }
    }

    /**
     * Whether the Compositor is currently suspended, 8 bits encoding the reason
     */
    suspend_reason m_suspended;
    QTimer m_releaseSelectionTimer;
    int m_framesToTestForSafety{3};

    std::unique_ptr<dbus::compositing<type>> dbus;

    // 2 sec which should be enough to restart the compositor.
    constexpr static auto compositor_lost_message_delay{2000};
    ulong s_msc{0};
};

}
