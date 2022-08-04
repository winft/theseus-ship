/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor.h"

#include "compositor_selection_owner.h"

#include "base/logging.h"
#include "debug/perf/ftrace.h"
#include "main.h"
#include "render/dbus/compositing.h"
#include "render/effects.h"
#include "render/gl/scene.h"
#include "render/platform.h"
#include "render/scene.h"
#include "render/shadow.h"
#include "render/window.h"
#include "render/xrender/scene.h"
#include "toplevel.h"
#include "win/remnant.h"
#include "win/space.h"
#include "win/space_window_release.h"
#include "win/stacking_order.h"
#include "win/transient.h"

#include <kwingl/texture.h>

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KNotification>
#include <stdexcept>
#include <xcb/composite.h>

namespace KWin::render::x11
{

static ulong s_msc = 0;

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

compositor::compositor(render::platform& platform)
    : render::compositor(platform)
    , m_suspended(kwinApp()->options->qobject->isUseCompositing() ? suspend_reason::none
                                                                  : suspend_reason::user)
{
    x11_integration.is_overlay_window = [this](auto win) { return checkForOverlayWindow(win); };
    x11_integration.update_blocking
        = [this](auto win) { return updateClientCompositeBlocking(win); };
    dbus->integration.get_types = [] { return QStringList{"glx"}; };
    dbus->integration.resume = [this] { resume(suspend_reason::script); };
    dbus->integration.suspend = [this] { suspend(suspend_reason::script); };

    if (qEnvironmentVariableIsSet("KWIN_MAX_FRAMES_TESTED")) {
        m_framesToTestForSafety = qEnvironmentVariableIntValue("KWIN_MAX_FRAMES_TESTED");
    }

    m_releaseSelectionTimer.setSingleShot(true);
    m_releaseSelectionTimer.setInterval(compositor_lost_message_delay);
    QObject::connect(&m_releaseSelectionTimer, &QTimer::timeout, qobject.get(), [this] {
        releaseCompositorSelection();
    });
    QObject::connect(qobject.get(),
                     &compositor_qobject::aboutToToggleCompositing,
                     qobject.get(),
                     [this] { overlay_window = nullptr; });
}

void compositor::start(win::space& space)
{
    if (!this->space) {
        // On first start setup connections.
        QObject::connect(kwinApp(), &Application::x11ConnectionChanged, qobject.get(), [this] {
            setupX11Support();
        });
        QObject::connect(space.stacking_order.get(),
                         &win::stacking_order::changed,
                         qobject.get(),
                         [this] { addRepaintFull(); });
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

    if (!platform.compositingPossible()) {
        qCCritical(KWIN_CORE) << "Compositing is not possible";
        return;
    }

    try {
        render::compositor::start_scene();
    } catch (std::runtime_error const& ex) {
        qCWarning(KWIN_CORE) << "Error: " << ex.what();
        qCWarning(KWIN_CORE) << "Compositing not possible. Continue without it.";

        m_state = State::Off;
        xcb_composite_unredirect_subwindows(
            kwinApp()->x11Connection(), kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
        destroyCompositorSelection();
    }
}

void compositor::schedule_repaint()
{
    if (isActive()) {
        setCompositeTimer();
    }
}

void compositor::schedule_repaint([[maybe_unused]] Toplevel* window)
{
    schedule_repaint();
}

void compositor::toggleCompositing()
{
    if (flags(m_suspended)) {
        // Direct user call; clear all bits.
        resume(suspend_reason::all);
    } else {
        // But only set the user one (sufficient to suspend).
        suspend(suspend_reason::user);
    }
}

void compositor::suspend(suspend_reason reason)
{
    assert(reason != suspend_reason::none);
    m_suspended |= reason;

    if (flags(reason & suspend_reason::script)) {
        // When disabled show a shortcut how the user can get back compositing.
        const auto shortcuts = KGlobalAccel::self()->shortcut(
            space->qobject->findChild<QAction*>(QStringLiteral("Suspend Compositing")));
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
    stop(false);
}

void compositor::resume(suspend_reason reason)
{
    assert(reason != suspend_reason::none);
    m_suspended &= ~reason;

    assert(space);
    start(*space);
}

void compositor::reinitialize()
{
    // Resume compositing if suspended.
    m_suspended = suspend_reason::none;
    // TODO(romangg): start the release selection timer?
    render::compositor::reinitialize();
}

void compositor::addRepaint(QRegion const& region)
{
    if (!isActive()) {
        return;
    }
    repaints_region += region;
    schedule_repaint();
}

void compositor::configChanged()
{
    if (flags(m_suspended)) {
        // TODO(romangg): start the release selection timer?
        stop(false);
        return;
    }
    render::compositor::configChanged();
}

bool compositor::checkForOverlayWindow(WId w) const
{
    if (!overlay_window) {
        // No overlay window, it cannot be the overlay.
        return false;
    }
    // Compare the window ID's.
    return w == overlay_window->window();
}

bool compositor::prepare_composition(QRegion& repaints, std::deque<Toplevel*>& windows)
{
    compositeTimer.stop();

    if (overlay_window && !overlay_window->isVisible()) {
        // Abort since nothing is visible.
        return false;
    }

    // If a buffer swap is still pending, we return to the event loop and
    // continue processing events until the swap has completed.
    if (m_bufferSwapPending) {
        return false;
    }

    // Create a list of all windows in the stacking order
    // Skip windows that are not yet ready for being painted.
    //
    // TODO? This cannot be used so carelessly - needs protections against broken clients, the
    // window should not get focus before it's displayed, handle unredirected windows properly and
    // so on.
    auto const& render_stack = win::render_stack(*space->stacking_order);
    std::copy_if(render_stack.begin(),
                 render_stack.end(),
                 std::back_inserter(windows),
                 [](auto const& win) { return win->ready_for_painting; });

    // Create a list of damaged windows and reset the damage state of each window and fetch the
    // damage region without waiting for a reply
    std::vector<Toplevel*> damaged;

    // Reserve a size for damaged to reduce reallocations when copying, its a bit larger then needed
    // but the exact size required us unknown beforehand.
    damaged.reserve(windows.size());

    std::copy_if(windows.begin(), windows.end(), std::back_inserter(damaged), [](auto const& win) {
        return win->resetAndFetchDamage();
    });

    if (damaged.size() > 0) {
        scene->triggerFence();
        if (auto c = kwinApp()->x11Connection()) {
            xcb_flush(c);
        }
    }

    // Move elevated windows to the top of the stacking order
    auto const elevated_win_list = effects->elevatedWindows();

    for (auto c : elevated_win_list) {
        auto t = static_cast<effects_window_impl*>(c)->window();
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

    if (auto const& wins = space->windows;
        repaints_region.isEmpty() && !std::any_of(wins.cbegin(), wins.cend(), [](auto const& win) {
            return win->has_pending_repaints();
        })) {
        scene->idle();

        // This means the next time we composite it is done without timer delay.
        m_delay = 0;
        return false;
    }

    repaints = repaints_region;

    // clear all repaints, so that post-pass can add repaints for the next repaint
    repaints_region = QRegion();

    return true;
}

template<typename Factory>
std::unique_ptr<render::scene>
create_scene_impl(x11::compositor& compositor, Factory& factory, std::string const& prev_err)
{
    auto setup_hooks = [&](auto& scene) {
        scene->windowing_integration.handle_viewport_limits_alarm = [&] {
            qCDebug(KWIN_CORE) << "Suspending compositing because viewport limits are not met";
            QTimer::singleShot(
                0, compositor.qobject.get(), [&] { compositor.suspend(suspend_reason::all); });
        };
    };

    try {
        auto scene = factory(compositor);
        setup_hooks(scene);
        if (!prev_err.empty()) {
            qCDebug(KWIN_CORE) << "Fallback after error:" << prev_err.c_str();
        }
        return scene;
    } catch (std::runtime_error const& exc) {
        throw std::runtime_error(prev_err + " " + exc.what());
    }
}

std::unique_ptr<render::scene> compositor::create_scene()
{
    using Factory = std::function<std::unique_ptr<render::scene>(compositor&)>;

    std::deque<Factory> factories;
    factories.push_back(gl::create_scene);

    auto const req_mode = kwinApp()->options->qobject->compositingMode();

#ifdef KWIN_HAVE_XRENDER_COMPOSITING
    if (req_mode == XRenderCompositing) {
        factories.push_front(xrender::create_scene);
    } else {
        factories.push_back(xrender::create_scene);
    }
#else
    if (req_mode == XRenderCompositing) {
        qCDebug(KWIN_CORE) << "Requested XRender compositing, but support has not been compiled. "
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

void compositor::performCompositing()
{
    QRegion repaints;
    std::deque<Toplevel*> windows;

    if (!prepare_composition(repaints, windows)) {
        return;
    }

    Perf::Ftrace::begin(QStringLiteral("Paint"), ++s_msc);
    create_opengl_safepoint(OpenGLSafePoint::PreFrame);

    auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

    // Start the actual painting process.
    auto const duration = scene->paint(repaints, windows, now);

    update_paint_periods(duration);
    create_opengl_safepoint(OpenGLSafePoint::PostFrame);
    retard_next_composition();

    for (auto win : windows) {
        if (win->remnant && !win->remnant->refcount) {
            win::delete_window_from_space(win->space, win);
        }
    }

    Perf::Ftrace::end(QStringLiteral("Paint"), s_msc);
}

void compositor::create_opengl_safepoint(OpenGLSafePoint safepoint)
{
    if (m_framesToTestForSafety <= 0) {
        return;
    }
    if (!(scene->compositingType() & OpenGLCompositing)) {
        return;
    }

    platform.createOpenGLSafePoint(safepoint);

    if (safepoint == OpenGLSafePoint::PostFrame) {
        if (--m_framesToTestForSafety == 0) {
            platform.createOpenGLSafePoint(OpenGLSafePoint::PostLastGuardedFrame);
        }
    }
}

void compositor::releaseCompositorSelection()
{
    switch (m_state) {
    case State::On:
        // We are compositing at the moment. Don't release.
        break;
    case State::Off:
        if (m_selectionOwner) {
            qCDebug(KWIN_CORE) << "Releasing compositor selection";
            m_selectionOwner->disown();
        }
        break;
    case State::Starting:
    case State::Stopping:
        // Still starting or shutting down the compositor. Starting might fail
        // or after stopping a restart might follow. So test again later on.
        m_releaseSelectionTimer.start();
        break;
    }
}

void compositor::updateClientCompositeBlocking(Toplevel* window)
{
    if (window) {
        if (window->isBlockingCompositing()) {
            // Do NOT attempt to call suspend(true) from within the eventchain!
            if (!(m_suspended & suspend_reason::rule))
                QMetaObject::invokeMethod(
                    qobject.get(),
                    [this]() { suspend(suspend_reason::rule); },
                    Qt::QueuedConnection);
        }
    } else if (flags(m_suspended & suspend_reason::rule)) {
        // If !c we just check if we can resume in case a blocking client was lost.
        bool shouldResume = true;

        for (auto const& client : space->windows) {
            if (client->isBlockingCompositing()) {
                shouldResume = false;
                break;
            }
        }
        if (shouldResume) {
            // Do NOT attempt to call suspend(false) from within the eventchain!
            QMetaObject::invokeMethod(
                qobject.get(), [this]() { resume(suspend_reason::rule); }, Qt::QueuedConnection);
        }
    }
}
}
