/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor.h"

#include "compositor_start.h"
#include "cursor.h"
#include "dbus/compositing.h"
#include "effects.h"
#include "platform.h"
#include "scene.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "base/wayland/server.h"
#include "debug/perf/ftrace.h"
#include "win/net.h"
#include "win/scene.h"
#include "win/space.h"
#include "win/stacking_order.h"

#include <QTimerEvent>
#include <stdexcept>
#include <xcb/composite.h>

namespace KWin::render
{

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

compositor::compositor(render::platform& platform)
    : qobject{std::make_unique<compositor_qobject>(
        [this](auto te) { return handle_timer_event(te); })}
    , platform{platform}
    , dbus{std::make_unique<dbus::compositing>(*this)}
{
    QObject::connect(kwinApp()->options->qobject.get(),
                     &base::options_qobject::configChanged,
                     qobject.get(),
                     [this] { configChanged(); });
    QObject::connect(kwinApp()->options->qobject.get(),
                     &base::options_qobject::animationSpeedChanged,
                     qobject.get(),
                     [this] { configChanged(); });

    m_unusedSupportPropertyTimer.setInterval(compositor_lost_message_delay);
    m_unusedSupportPropertyTimer.setSingleShot(true);
    QObject::connect(&m_unusedSupportPropertyTimer, &QTimer::timeout, qobject.get(), [this] {
        deleteUnusedSupportProperties();
    });
}

compositor::~compositor()
{
    Q_EMIT qobject->aboutToDestroy();
    compositor_stop(*this, true);
    deleteUnusedSupportProperties();
    compositor_destroy_selection(*this);
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
    if (m_state == state::starting || m_state == state::stopping) {
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
    compositor_stop(*this, false);

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

bool compositor::handle_timer_event(QTimerEvent* te)
{
    if (te->timerId() != compositeTimer.timerId()) {
        return false;
    }
    performCompositing();
    return true;
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
    compositeTimer.start(qMin(waitTime, 250u), qobject.get());
}

bool compositor::isActive()
{
    return m_state == state::on;
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
