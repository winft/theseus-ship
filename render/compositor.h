/*
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor_qobject.h"
#include "compositor_start.h"
#include "cursor.h"
#include "dbus/compositing.h"
#include "effects.h"
#include "types.h"

#include "base/platform.h"
#include "debug/perf/ftrace.h"
#include "kwinglobals.h"
#include "main.h"

#include <QBasicTimer>
#include <QRegion>
#include <QTimer>
#include <deque>
#include <map>
#include <memory>

namespace KWin::render
{

template<typename Window>
struct compositor_x11_integration {
    std::function<bool(xcb_window_t)> is_overlay_window;
    std::function<void(Window*)> update_blocking;
};

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

template<typename Platform>
class compositor
{
public:
    using space_t = typename Platform::base_t::space_t;
    using scene_t = render::scene<Platform>;

    explicit compositor(Platform& platform)
        : qobject{std::make_unique<compositor_qobject>(
            [this](auto te) { return handle_timer_event(te); })}
        , platform{platform}
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

    virtual ~compositor() = default;

    virtual void start(space_t& space) = 0;

    // when adding repaints caused by a window, you probably want to use
    // either Toplevel::addRepaint() or Toplevel::addWorkspaceRepaint()
    virtual void addRepaint(QRegion const& /*region*/)
    {
    }

    void addRepaintFull()
    {
        auto const& space_size = kwinApp()->get_base().topology.size;
        addRepaint(QRegion(0, 0, space_size.width(), space_size.height()));
    }

    /**
     * Schedules a new repaint if no repaint is currently scheduled. Tries to optimize by only
     * repainting outputs that the visible bounds of @arg window intersect with.
     */
    virtual void schedule_repaint(typename space_t::window_t* /*window*/)
    {
        // Needs to be implemented because might get called on destructor.
        // TODO(romangg): Remove this, i.e. ensure that there are no calls while being destroyed.
    }

    virtual void schedule_frame_callback(typename space_t::window_t* /*window*/)
    {
        // Only needed on Wayland.
    }

    /**
     * Notifies the compositor that SwapBuffers() is about to be called.
     * Rendering of the next frame will be deferred until bufferSwapComplete()
     * is called.
     */
    void aboutToSwapBuffers()
    {
        assert(!m_bufferSwapPending);
        m_bufferSwapPending = true;
    }

    /**
     * Notifies the compositor that a pending buffer swap has completed.
     */
    virtual void bufferSwapComplete(bool present = true)
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

    /**
     * Toggles compositing, that is if the Compositor is suspended it will be resumed
     * and if the Compositor is active it will be suspended.
     * Invoked by keybinding (shortcut default: Shift + Alt + F12).
     */
    virtual void toggleCompositing() = 0;

    /**
     * Whether the Compositor is active. That is a Scene is present and the Compositor is
     * not shutting down itself.
     */
    bool isActive()
    {
        return m_state == state::on;
    }

    // for delayed supportproperty management of effects
    void keepSupportProperty(xcb_atom_t atom)
    {
        m_unusedSupportProperties.removeAll(atom);
    }

    void removeSupportProperty(xcb_atom_t atom)
    {
        m_unusedSupportProperties << atom;
        m_unusedSupportPropertyTimer.start();
    }

    virtual std::unique_ptr<scene_t> create_scene() = 0;

    virtual void performCompositing() = 0;
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

    virtual void configChanged() = 0;

    bool handle_timer_event(QTimerEvent* te)
    {
        if (te->timerId() != compositeTimer.timerId()) {
            return false;
        }
        performCompositing();
        return true;
    }

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

    void deleteUnusedSupportProperties()
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

    /**
     * The current refresh cycle length. In the future this should be per output on Wayland.
     *
     * @return refresh cycle length in nanoseconds.
     */
    qint64 refreshLength() const
    {
        return 1000 * 1000 / qint64(refreshRate());
    }

    std::unique_ptr<compositor_qobject> qobject;

    std::unique_ptr<scene_t> scene;

    // TODO(romangg): Only relevant for Wayland. Put in child class.
    std::unique_ptr<cursor<Platform>> software_cursor;
    compositor_x11_integration<typename space_t::window_t> x11_integration;

    Platform& platform;
    space_t* space{nullptr};

    state m_state{state::off};
    x11::compositor_selection_owner* m_selectionOwner{nullptr};
    QRegion repaints_region;
    QBasicTimer compositeTimer;
    qint64 m_delay{0};
    bool m_bufferSwapPending{false};

    QList<xcb_atom_t> m_unusedSupportProperties;
    QTimer m_unusedSupportPropertyTimer;

    // Compositing delay (in ns).
    qint64 m_lastPaintDurations[2]{0};
    int m_paintPeriods{0};
};

}
