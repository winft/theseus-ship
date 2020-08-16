/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2020 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "presentation.h"

#include "abstract_wayland_output.h"
#include "main.h"
#include "platform.h"
#include "toplevel.h"
#include "wayland_server.h"

#include <Wrapland/Server/output.h>
#include <Wrapland/Server/presentation_time.h>
#include <Wrapland/Server/surface.h>

#include <QElapsedTimer>

#define NSEC_PER_SEC 1000000000

namespace KWin
{

Presentation::Presentation(QObject *parent)
    : QObject(parent)
{
}

Presentation::~Presentation()
{
    delete m_fallbackClock;
}

bool Presentation::initClock(bool clockIdValid, clockid_t clockId)
{
    if (clockIdValid) {
        m_clockId = clockId;

        struct timespec ts;
        if (clock_gettime(clockId, &ts) != 0) {
            qCWarning(KWIN_CORE) << "Could not get presentation clock.";
            return false;
        }

    } else {
        // There might be other clock types, but for now assume it is always monotonic or realtime.
        clockId = QElapsedTimer::isMonotonic() ? CLOCK_MONOTONIC : CLOCK_REALTIME;

        m_fallbackClock = new QElapsedTimer();
        m_fallbackClock->start();
    }

    if (!waylandServer()->presentationManager()) {
        waylandServer()->createPresentationManager();
    }
    waylandServer()->presentationManager()->setClockId(clockId);

    return true;
}

uint32_t Presentation::currentTime() const
{
    if (m_fallbackClock) {
        return m_fallbackClock->elapsed();
    }

    uint32_t currentTime = 0;
    timespec ts;
    if (clock_gettime(m_clockId, &ts) == 0) {
        currentTime = ts.tv_sec * 1000 + ts.tv_nsec / 1000 / 1000;
    }
    return currentTime;
}

void Presentation::lock(AbstractWaylandOutput* output, const QList<Toplevel*> windows)
{
    auto const now = currentTime();

    // TODO(romangg): what to do when the output gets removed or disabled while we have locked
    // surfaces?

    for (auto *win : windows) {
        auto *surface = win->surface();
        if (!surface) {
            continue;
        }

        surface->frameRendered(now);

        auto const id = surface->lockPresentation(output->output());
        if (id != 0) {
            m_surfaces[id] = surface;
            connect(surface, &Wrapland::Server::Surface::resourceDestroyed,
                    this, [this, id, surface]() { m_surfaces.remove(id); });
        }
    }
}

Wrapland::Server::Surface::PresentationKinds toKinds(Presentation::Kinds kinds)
{
    using Kind = Presentation::Kind;
    using RetKind = Wrapland::Server::Surface::PresentationKind;

    Wrapland::Server::Surface::PresentationKinds ret;
    if (kinds.testFlag(Kind::Vsync)) {
        ret |= RetKind::Vsync;
    }
    if (kinds.testFlag(Kind::HwClock)) {
        ret |= RetKind::HwClock;
    }
    if (kinds.testFlag(Kind::HwCompletion)) {
        ret |= RetKind::HwCompletion;
    }
    if (kinds.testFlag(Kind::ZeroCopy)) {
        ret |= RetKind::ZeroCopy;
    }
    return ret;
}

// From Weston.
void timespecToProto(const timespec &ts, uint32_t &tvSecHi,
                     uint32_t &tvSecLo, uint32_t &tvNsec)
{
    Q_ASSERT(ts.tv_sec >= 0);
    Q_ASSERT(ts.tv_nsec >= 0 && ts.tv_nsec < NSEC_PER_SEC);

    uint64_t sec64 = ts.tv_sec;

    tvSecHi = sec64 >> 32;
    tvSecLo = sec64 & 0xffffffff;
    tvNsec = ts.tv_nsec;
}

void Presentation::presented(AbstractWaylandOutput* output, uint32_t sec, uint32_t usec, Kinds kinds)
{
    if (!output->isEnabled()) {
        // Output disabled, discards will be sent from Wrapland.
        return;
    }

    timespec ts;
    ts.tv_sec = sec;
    ts.tv_nsec = usec * 1000;

    uint32_t tvSecHi;
    uint32_t tvSecLo;
    uint32_t tvNsec;
    timespecToProto(ts, tvSecHi, tvSecLo, tvNsec);

    auto const refreshRate = output->refreshRate();
    Q_ASSERT(refreshRate > 0);
    auto const refreshLength = 1 / (double)refreshRate;
    uint32_t const refresh = refreshLength * 1000 * 1000 * 1000 * 1000;
    auto const msc = output->msc();

    auto it = m_surfaces.constBegin();
    while (it != m_surfaces.constEnd()) {
        auto *surface = it.value();
        surface->presentationFeedback(it.key(), tvSecHi, tvSecLo, tvNsec,
                                      refresh,
                                      msc >> 32, msc & 0xffffffff, toKinds(kinds));
        disconnect(surface, &Wrapland::Server::Surface::resourceDestroyed,
                   this, nullptr);
        ++it;
    }
    m_surfaces.clear();
}

void Presentation::softwarePresented(Kinds kinds)
{
    int64_t const elapsedTime = m_fallbackClock->nsecsElapsed();
    uint32_t const elapsedSeconds = static_cast<double>(elapsedTime) / NSEC_PER_SEC;
    uint32_t const nanoSecondsPart
        = elapsedTime - static_cast<int64_t>(elapsedSeconds) * NSEC_PER_SEC;

    timespec ts;
    ts.tv_sec = elapsedSeconds;
    ts.tv_nsec = nanoSecondsPart;

    uint32_t tvSecHi;
    uint32_t tvSecLo;
    uint32_t tvNsec;
    timespecToProto(ts, tvSecHi, tvSecLo, tvNsec);

    auto output = static_cast<AbstractWaylandOutput*>(kwinApp()->platform()->enabledOutputs()[0]);
    const int refreshRate = output->refreshRate();
    Q_ASSERT(refreshRate > 0);
    const double refreshLength = 1 / (double)refreshRate;
    const uint32_t refresh = refreshLength * 1000 * 1000 * 1000 * 1000;

    uint64_t const seq = m_fallbackClock->elapsed() / (double)refreshRate;

    auto it = m_surfaces.constBegin();
    while (it != m_surfaces.constEnd()) {
        auto *surface = it.value();
        surface->presentationFeedback(it.key(), tvSecHi, tvSecLo, tvNsec,
                                      refresh,
                                      seq >> 32, seq & 0xffffffff, toKinds(kinds));
        disconnect(surface, &Wrapland::Server::Surface::resourceDestroyed,
                   this, nullptr);
        ++it;
    }
    m_surfaces.clear();
}

}
