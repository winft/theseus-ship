/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor.h"

#include "cursor.h"
#include "dbus/compositing.h"
#include "effects.h"
#include "platform.h"
#include "scene.h"
#include "x11/compositor_selection_owner.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "base/wayland/server.h"
#include "debug/perf/ftrace.h"
#include "win/net.h"
#include "win/remnant.h"
#include "win/scene.h"
#include "win/space.h"
#include "win/space_window_release.h"
#include "win/stacking_order.h"
#include "win/x11/stacking_tree.h"

#include <QTimerEvent>
#include <stdexcept>

namespace KWin::render
{

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

bool compositor::compositing()
{
    auto const& compositor = kwinApp()->get_base().render->compositor;
    return compositor && compositor->isActive();
}

compositor::compositor(render::platform& platform)
    : platform{platform}
    , dbus{std::make_unique<dbus::compositing>(*this)}
{
    connect(
        kwinApp()->options.get(), &base::options::configChanged, this, &compositor::configChanged);
    connect(kwinApp()->options.get(),
            &base::options::animationSpeedChanged,
            this,
            &compositor::configChanged);

    m_unusedSupportPropertyTimer.setInterval(compositor_lost_message_delay);
    m_unusedSupportPropertyTimer.setSingleShot(true);
    connect(&m_unusedSupportPropertyTimer,
            &QTimer::timeout,
            this,
            &compositor::deleteUnusedSupportProperties);
}

compositor::~compositor()
{
    Q_EMIT aboutToDestroy();
    stop(true);
    deleteUnusedSupportProperties();
    destroyCompositorSelection();
}

void compositor::start_scene()
{
    assert(space);
    assert(!scene);

    if (kwinApp()->isTerminating()) {
        // Don't start while KWin is terminating. An event to restart might be lingering
        // in the event queue due to graphics reset.
        return;
    }

    if (m_state != State::Off) {
        return;
    }

    m_state = State::Starting;
    kwinApp()->options->reloadCompositingSettings(true);
    setupX11Support();
    Q_EMIT aboutToToggleCompositing();

    scene = create_scene();
    space->x_stacking_tree->mark_as_dirty();
    for (auto& client : space->windows()) {
        client->setupCompositing();
    }

    // Sets also the 'effects' pointer.
    effects = platform.createEffectsHandler(this, scene.get());
    connect(
        effects.get(), &EffectsHandler::screenGeometryChanged, this, &compositor::addRepaintFull);
    connect(space->stacking_order.get(), &win::stacking_order::unlocked, this, [this]() {
        if (effects) {
            effects->checkInputWindowStacking();
        }
    });

    m_state = State::On;
    Q_EMIT compositingToggled(true);

    // Render at least once.
    addRepaintFull();
    performCompositing();
}

void compositor::claimCompositorSelection()
{
    using CompositorSelectionOwner = x11::compositor_selection_owner;

    if (!m_selectionOwner) {
        char selection_name[100];
        sprintf(selection_name, "_NET_WM_CM_S%d", kwinApp()->x11ScreenNumber());
        m_selectionOwner = new CompositorSelectionOwner(selection_name);
        connect(m_selectionOwner, &CompositorSelectionOwner::lostOwnership, this, [this] {
            stop(false);
        });
    }

    if (!m_selectionOwner) {
        // No X11 yet.
        return;
    }

    m_selectionOwner->own();
}

void compositor::setupX11Support()
{
    auto con = kwinApp()->x11Connection();
    if (!con) {
        delete m_selectionOwner;
        m_selectionOwner = nullptr;
        return;
    }
    claimCompositorSelection();
    xcb_composite_redirect_subwindows(
        con, kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
}

void compositor::schedule_repaint(Toplevel* /*window*/)
{
    // Needs to be implemented because might get called on destructor.
    // TODO(romangg): Remove this, i.e. ensure that there are no calls while being destroyed.
}

void compositor::schedule_frame_callback(Toplevel* /*window*/)
{
    // Only needed on Wayland.
}

void compositor::stop(bool on_shutdown)
{
    if (m_state == State::Off || m_state == State::Stopping) {
        return;
    }
    m_state = State::Stopping;
    Q_EMIT aboutToToggleCompositing();

    // Some effects might need access to effect windows when they are about to
    // be destroyed, for example to unreference deleted windows, so we have to
    // make sure that effect windows outlive effects.
    effects.reset();

    if (space) {
        for (auto& c : space->windows()) {
            if (c->remnant) {
                continue;
            }
            c->finishCompositing();
        }

        if (auto con = kwinApp()->x11Connection()) {
            xcb_composite_unredirect_subwindows(
                con, kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
        }
        while (!space->remnants().empty()) {
            auto win = space->remnants().front();
            win->remnant->refcount = 0;
            win::delete_window_from_space(win->space, win);
        }
    }

    assert(scene);
    scene.reset();
    platform.render_stop(on_shutdown);

    m_bufferSwapPending = false;
    compositeTimer.stop();
    repaints_region = QRegion();

    m_state = State::Off;
    Q_EMIT compositingToggled(false);
}

void compositor::destroyCompositorSelection()
{
    delete m_selectionOwner;
    m_selectionOwner = nullptr;
}

void compositor::keepSupportProperty(xcb_atom_t atom)
{
    m_unusedSupportProperties.removeAll(atom);
}

void compositor::removeSupportProperty(xcb_atom_t atom)
{
    m_unusedSupportProperties << atom;
    m_unusedSupportPropertyTimer.start();
}

void compositor::deleteUnusedSupportProperties()
{
    if (m_state == State::Starting || m_state == State::Stopping) {
        // Currently still maybe restarting the compositor.
        m_unusedSupportPropertyTimer.start();
        return;
    }
    if (auto con = kwinApp()->x11Connection()) {
        for (auto const& atom : qAsConst(m_unusedSupportProperties)) {
            // remove property from root window
            xcb_delete_property(con, kwinApp()->x11RootWindow(), atom);
        }
        m_unusedSupportProperties.clear();
    }
}

void compositor::configChanged()
{
    reinitialize();
    addRepaintFull();
}

void compositor::reinitialize()
{
    // Reparse config. Config options will be reloaded by start()
    kwinApp()->config()->reparseConfiguration();

    // Restart compositing
    stop(false);

    assert(space);
    start(*space);

    if (effects) {
        // start() may fail
        effects->reconfigure();
    }
}

void compositor::addRepaint([[maybe_unused]] QRegion const& region)
{
}

void compositor::addRepaintFull()
{
    auto const& space_size = kwinApp()->get_base().topology.size;
    addRepaint(QRegion(0, 0, space_size.width(), space_size.height()));
}

void compositor::timerEvent(QTimerEvent* te)
{
    if (te->timerId() == compositeTimer.timerId()) {
        performCompositing();
    } else
        QObject::timerEvent(te);
}

void compositor::aboutToSwapBuffers()
{
    assert(!m_bufferSwapPending);
    m_bufferSwapPending = true;
}

void compositor::bufferSwapComplete(bool present)
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

void compositor::update_paint_periods(int64_t duration)
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

void compositor::retard_next_composition()
{
    if (scene->hasSwapEvent()) {
        // We wait on an explicit callback from the backend to unlock next composition runs.
        return;
    }
    m_delay = refreshLength();
    setCompositeTimer();
}

qint64 compositor::refreshLength() const
{
    return 1000 * 1000 / qint64(refreshRate());
}

void compositor::setCompositeTimer()
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
    compositeTimer.start(qMin(waitTime, 250u), this);
}

bool compositor::isActive()
{
    return m_state == State::On;
}

int compositor::refreshRate() const
{
    int max_refresh_rate = 60000;
    for (auto output : platform.base.get_outputs()) {
        auto const rate = output->refresh_rate();
        if (rate > max_refresh_rate) {
            max_refresh_rate = rate;
        }
    }
    return max_refresh_rate;
}

}
