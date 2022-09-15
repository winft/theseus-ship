/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor_selection_owner.h"
#include "overlay_window.h"
#include "types.h"

#include "debug/perf/ftrace.h"
#include "render/compositor.h"
#include "render/compositor_start.h"
#include "render/dbus/compositing.h"
#include "render/effect/window_impl.h"
#include "render/gl/scene.h"
#include "render/platform.h"
#include "render/xrender/scene.h"
#include "win/remnant.h"
#include "win/space_window_release.h"
#include "win/stacking_order.h"

#include <KGlobalAccel>
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
class compositor : public render::compositor<Platform>
{
public:
    using platform_t = Platform;
    using type = compositor<Platform>;
    using abstract_type = render::compositor<Platform>;
    using effects_t = effects_handler_impl<type>;
    using overlay_window_t = x11::overlay_window<type>;
    using space_t = typename abstract_type::space_t;
    using window_t = typename space_t::window_t::render_t;
    using effect_window_t = typename window_t::effect_window_t;

    compositor(Platform& platform)
        : render::compositor<Platform>(platform)
        , m_suspended(kwinApp()->options->qobject->isUseCompositing() ? suspend_reason::none
                                                                      : suspend_reason::user)
        , dbus{std::make_unique<dbus::compositing<type>>(*this)}
    {
        this->x11_integration.is_overlay_window
            = [this](auto win) { return checkForOverlayWindow(win); };
        this->x11_integration.update_blocking
            = [this](auto win) { return updateClientCompositeBlocking(win); };
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

    ~compositor() override
    {
        Q_EMIT this->qobject->aboutToDestroy();
        compositor_stop(*this, true);
        this->deleteUnusedSupportProperties();
        compositor_destroy_selection(*this);
    }

    void start(space_t& space) override
    {
        if (!this->space) {
            // On first start setup connections.
            QObject::connect(kwinApp(),
                             &Application::x11ConnectionChanged,
                             this->qobject.get(),
                             [this] { compositor_setup_x11_support(*this); });
            QObject::connect(space.stacking.order.qobject.get(),
                             &win::stacking_order_qobject::changed,
                             this->qobject.get(),
                             [this] { this->addRepaintFull(); });
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

            this->m_state = state::off;
            xcb_composite_unredirect_subwindows(kwinApp()->x11Connection(),
                                                kwinApp()->x11RootWindow(),
                                                XCB_COMPOSITE_REDIRECT_MANUAL);
            compositor_destroy_selection(*this);
        }
    }

    void schedule_repaint()
    {
        if (this->isActive()) {
            this->setCompositeTimer();
        }
    }

    void schedule_repaint(typename space_t::window_t* /*window*/) override
    {
        schedule_repaint();
    }

    void toggleCompositing() override
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
     * Note: it is possible that the Compositor is not able to suspend. Use isActive to check
     * whether the Compositor has been suspended.
     *
     * @return void
     * @see resume
     * @see isActive
     */
    void suspend(suspend_reason reason)
    {
        assert(reason != suspend_reason::none);
        m_suspended |= reason;

        if (flags(reason & suspend_reason::script)) {
            // When disabled show a shortcut how the user can get back compositing.
            const auto shortcuts
                = KGlobalAccel::self()->shortcut(this->space->qobject->template findChild<QAction*>(
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
     * blocking the usage of Compositing or the Scene might be broken. Use isActive to check
     * whether the Compositor has been resumed. Also check isCompositingPossible and
     * isOpenGLBroken.
     *
     * Note: The starting of the Compositor can require some time and is partially done threaded.
     * After this method returns the setup may not have been completed.
     *
     * @return void
     * @see suspend
     * @see isActive
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

    void addRepaint(QRegion const& region) override
    {
        if (!this->isActive()) {
            return;
        }
        this->repaints_region += region;
        schedule_repaint();
    }

    void configChanged() override
    {
        if (flags(m_suspended)) {
            // TODO(romangg): start the release selection timer?
            compositor_stop(*this, false);
            return;
        }
        reinitialize();
        this->addRepaintFull();
    }

    /**
     * Checks whether @p w is the Scene's overlay window.
     */
    bool checkForOverlayWindow(WId w) const
    {
        if (!overlay_window) {
            // No overlay window, it cannot be the overlay.
            return false;
        }
        // Compare the window ID's.
        return w == overlay_window->window();
    }

    void updateClientCompositeBlocking(typename space_t::window_t* window)
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

            for (auto const& client : this->space->windows) {
                if (client->isBlockingCompositing()) {
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

    std::unique_ptr<render::scene<Platform>> create_scene() override
    {
        using Factory = std::function<std::unique_ptr<render::scene<Platform>>(Platform&)>;

        std::deque<Factory> factories;
        factories.push_back(gl::create_scene<Platform>);

        auto const req_mode = kwinApp()->options->qobject->compositingMode();

#ifdef KWIN_HAVE_XRENDER_COMPOSITING
        if (req_mode == XRenderCompositing) {
            factories.push_front(xrender::create_scene<Platform>);
        } else {
            factories.push_back(xrender::create_scene<Platform>);
        }
#else
        if (req_mode == XRenderCompositing) {
            qCDebug(KWIN_CORE)
                << "Requested XRender compositing, but support has not been compiled. "
                   "Continue with OpenGL.";
        }
#endif

        try {
            return create_scene_impl(*this, factories.at(0), "");
        } catch (std::runtime_error const& exc) {
            if (factories.size() > 1) {
                return create_scene_impl(*this, factories.at(1), exc.what());
            }
            throw exc;
        }
    }

    void performCompositing() override
    {
        QRegion repaints;
        std::deque<typename space_t::window_t*> windows;

        if (!prepare_composition(repaints, windows)) {
            return;
        }

        Perf::Ftrace::begin(QStringLiteral("Paint"), ++s_msc);
        create_opengl_safepoint(OpenGLSafePoint::PreFrame);

        auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

        // Start the actual painting process.
        auto const duration = this->scene->paint(repaints, windows, now);

        this->update_paint_periods(duration);
        create_opengl_safepoint(OpenGLSafePoint::PostFrame);
        this->retard_next_composition();

        for (auto win : windows) {
            if (win->remnant && !win->remnant->refcount) {
                win::delete_window_from_space(win->space, win);
            }
        }

        Perf::Ftrace::end(QStringLiteral("Paint"), s_msc);
    }

    std::unique_ptr<effects_t> effects;

private:
    void releaseCompositorSelection()
    {
        switch (this->m_state) {
        case state::on:
            // We are compositing at the moment. Don't release.
            break;
        case state::off:
            if (this->m_selectionOwner) {
                qCDebug(KWIN_CORE) << "Releasing compositor selection";
                this->m_selectionOwner->disown();
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

    bool prepare_composition(QRegion& repaints, std::deque<typename space_t::window_t*>& windows)
    {
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
        // Skip windows that are not yet ready for being painted.
        //
        // TODO? This cannot be used so carelessly - needs protections against broken clients, the
        // window should not get focus before it's displayed, handle unredirected windows properly
        // and so on.
        auto const& render_stack = win::render_stack(this->space->stacking.order);
        std::copy_if(render_stack.begin(),
                     render_stack.end(),
                     std::back_inserter(windows),
                     [](auto const& win) { return win->ready_for_painting; });

        // Create a list of damaged windows and reset the damage state of each window and fetch the
        // damage region without waiting for a reply
        std::vector<typename space_t::window_t*> damaged;

        // Reserve a size for damaged to reduce reallocations when copying, its a bit larger then
        // needed but the exact size required us unknown beforehand.
        damaged.reserve(windows.size());

        std::copy_if(windows.begin(),
                     windows.end(),
                     std::back_inserter(damaged),
                     [](auto const& win) { return win->resetAndFetchDamage(); });

        if (damaged.size() > 0) {
            this->scene->triggerFence();
            if (auto c = kwinApp()->x11Connection()) {
                xcb_flush(c);
            }
        }

        // Move elevated windows to the top of the stacking order
        auto const elevated_win_list = this->effects->elevatedWindows();

        for (auto c : elevated_win_list) {
            auto t = static_cast<effect_window_t*>(c)->window.ref_win;
            if (!move_to_back(windows, t)) {
                windows.push_back(t);
            }
        }

        // Discard the lanczos texture
        auto discard_lanczos_texture = [](auto window) {
            assert(window->render);
            assert(window->render->effect);

            auto const texture = window->render->effect->data(LanczosCacheRole);
            if (texture.isValid()) {
                delete static_cast<GLTexture*>(texture.template value<void*>());
                window->render->effect->setData(LanczosCacheRole, QVariant());
            }
        };

        // Get the replies
        for (auto win : damaged) {
            discard_lanczos_texture(win);
            win->getDamageRegionReply();
        }

        if (auto const& wins = this->space->windows; this->repaints_region.isEmpty()
            && !std::any_of(wins.cbegin(), wins.cend(), [](auto const& win) {
                                                         return win->has_pending_repaints();
                                                     })) {
            this->scene->idle();

            // This means the next time we composite it is done without timer delay.
            this->m_delay = 0;
            return false;
        }

        repaints = this->repaints_region;

        // clear all repaints, so that post-pass can add repaints for the next repaint
        this->repaints_region = QRegion();

        return true;
    }

    void create_opengl_safepoint(OpenGLSafePoint safepoint)
    {
        if (m_framesToTestForSafety <= 0) {
            return;
        }
        if (!(this->scene->compositingType() & OpenGLCompositing)) {
            return;
        }

        this->platform.createOpenGLSafePoint(safepoint);

        if (safepoint == OpenGLSafePoint::PostFrame) {
            if (--m_framesToTestForSafety == 0) {
                this->platform.createOpenGLSafePoint(OpenGLSafePoint::PostLastGuardedFrame);
            }
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
