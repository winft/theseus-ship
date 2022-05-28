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
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/x11/stacking_tree.h"

#include <kwingl/texture.h>

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KNotification>

Q_DECLARE_METATYPE(KWin::render::x11::compositor::SuspendReason)

namespace KWin::render::x11
{

static ulong s_msc = 0;

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

compositor::compositor(render::platform& platform)
    : render::compositor(platform)
    , m_suspended(kwinApp()->options->isUseCompositing() ? NoReasonSuspend : UserSuspend)
{
    x11_integration.is_overlay_window = [this](auto win) { return checkForOverlayWindow(win); };
    x11_integration.update_blocking
        = [this](auto win) { return updateClientCompositeBlocking(win); };
    dbus->integration.get_types = [] { return QStringList{"glx"}; };
    dbus->integration.resume = [this] { resume(ScriptSuspend); };
    dbus->integration.suspend = [this] { suspend(ScriptSuspend); };

    if (qEnvironmentVariableIsSet("KWIN_MAX_FRAMES_TESTED")) {
        m_framesToTestForSafety = qEnvironmentVariableIntValue("KWIN_MAX_FRAMES_TESTED");
    }

    m_releaseSelectionTimer.setSingleShot(true);
    m_releaseSelectionTimer.setInterval(compositor_lost_message_delay);
    connect(
        &m_releaseSelectionTimer, &QTimer::timeout, this, &compositor::releaseCompositorSelection);
    QObject::connect(
        this, &compositor::aboutToToggleCompositing, this, [this] { overlay_window = nullptr; });
}

void compositor::start(win::space& space)
{
    if (!this->space) {
        // On first start setup connections.
        QObject::connect(
            kwinApp(), &Application::x11ConnectionChanged, this, &compositor::setupX11Support);
        QObject::connect(space.stacking_order.get(),
                         &win::stacking_order::changed,
                         this,
                         &compositor::addRepaintFull);
        this->space = &space;
    }

    if (m_suspended) {
        QStringList reasons;
        if (m_suspended & UserSuspend) {
            reasons << QStringLiteral("Disabled by User");
        }
        if (m_suspended & BlockRuleSuspend) {
            reasons << QStringLiteral("Disabled by Window");
        }
        if (m_suspended & ScriptSuspend) {
            reasons << QStringLiteral("Disabled by Script");
        }
        qCDebug(KWIN_CORE) << "Compositing is suspended, reason:" << reasons;
        return;
    }
    if (!platform.compositingPossible()) {
        qCCritical(KWIN_CORE) << "Compositing is not possible";
        return;
    }

    if (!render::compositor::setupStart()) {
        // Internal setup failed, abort.
        return;
    }

    if (m_releaseSelectionTimer.isActive()) {
        m_releaseSelectionTimer.stop();
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
    if (m_suspended) {
        // Direct user call; clear all bits.
        resume(AllReasonSuspend);
    } else {
        // But only set the user one (sufficient to suspend).
        suspend(UserSuspend);
    }
}

void compositor::suspend(compositor::SuspendReason reason)
{
    assert(reason != NoReasonSuspend);
    m_suspended |= reason;

    if (reason & ScriptSuspend) {
        // When disabled show a shortcut how the user can get back compositing.
        const auto shortcuts = KGlobalAccel::self()->shortcut(
            space->findChild<QAction*>(QStringLiteral("Suspend Compositing")));
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

void compositor::resume(compositor::SuspendReason reason)
{
    assert(reason != NoReasonSuspend);
    m_suspended &= ~reason;

    assert(space);
    start(*space);
}

void compositor::reinitialize()
{
    // Resume compositing if suspended.
    m_suspended = NoReasonSuspend;
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
    if (m_suspended) {
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
    windows = space->x_stacking_tree->as_list();
    std::vector<Toplevel*> damaged;

    // Reset the damage state of each window and fetch the damage region
    // without waiting for a reply
    for (auto win : windows) {
        if (win->resetAndFetchDamage()) {
            damaged.push_back(win);
        }
    }

    if (damaged.size() > 0) {
        scene->triggerFence();
        if (auto c = kwinApp()->x11Connection()) {
            xcb_flush(c);
        }
    }

    // Move elevated windows to the top of the stacking order
    auto const elevated_win_list = static_cast<effects_handler_impl*>(effects)->elevatedWindows();

    for (auto c : elevated_win_list) {
        auto t = static_cast<effects_window_impl*>(c)->window();
        remove_all(windows, t);
        windows.push_back(t);
    }

    // Get the replies
    for (auto win : damaged) {
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

        win->getDamageRegionReply();
    }

    if (auto const& wins = space->m_windows;
        repaints_region.isEmpty() && !std::any_of(wins.cbegin(), wins.cend(), [](auto const& win) {
            return win->has_pending_repaints();
        })) {
        scene->idle();

        // This means the next time we composite it is done without timer delay.
        m_delay = 0;
        return false;
    }

    // Skip windows that are not yet ready for being painted.
    //
    // TODO? This cannot be used so carelessly - needs protections against broken clients, the
    // window should not get focus before it's displayed, handle unredirected windows properly and
    // so on.

    remove_all_if(windows, [](auto const& win) { return !win->readyForPainting(); });

    repaints = repaints_region;

    // clear all repaints, so that post-pass can add repaints for the next repaint
    repaints_region = QRegion();

    return true;
}

render::scene* compositor::create_scene(QVector<CompositingType> const& support)
{
    render::scene* scene{nullptr};

    for (auto type : support) {
        if (type == OpenGLCompositing) {
            qCDebug(KWIN_CORE) << "Creating OpenGL scene.";
            scene = gl::create_scene(*this);
            break;
        }
#ifdef KWIN_HAVE_XRENDER_COMPOSITING
        if (type == XRenderCompositing) {
            qCDebug(KWIN_CORE) << "Creating XRender scene.";
            scene = xrender::create_scene(*this);
            break;
        }
#endif
    }

    if (scene) {
        scene->windowing_integration.handle_viewport_limits_alarm = [this] {
            qCDebug(KWIN_CORE) << "Suspending compositing because viewport limits are not met";
            QTimer::singleShot(
                0, this, [this] { suspend(render::x11::compositor::AllReasonSuspend); });
        };
    }

    return scene;
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
        if (win->remnant() && !win->remnant()->refcount) {
            delete win;
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
            if (!(m_suspended & BlockRuleSuspend))
                QMetaObject::invokeMethod(
                    this, [this]() { suspend(BlockRuleSuspend); }, Qt::QueuedConnection);
        }
    } else if (m_suspended & BlockRuleSuspend) {
        // If !c we just check if we can resume in case a blocking client was lost.
        bool shouldResume = true;

        for (auto const& client : space->m_windows) {
            if (client->isBlockingCompositing()) {
                shouldResume = false;
                break;
            }
        }
        if (shouldResume) {
            // Do NOT attempt to call suspend(false) from within the eventchain!
            QMetaObject::invokeMethod(
                this, [this]() { resume(BlockRuleSuspend); }, Qt::QueuedConnection);
        }
    }
}

}
