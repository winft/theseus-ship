/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QHash>
#include <QObject>

#include <deque>
#include <time.h>

class QElapsedTimer;

namespace Wrapland::Server
{
class PresentationFeedback;
class Surface;
}

namespace KWin
{

class AbstractWaylandOutput;
class Toplevel;

namespace render::wayland
{
class output;

class KWIN_EXPORT presentation : public QObject
{
    Q_OBJECT
public:
    enum class kind {
        None = 0,
        Vsync = 1 << 0,
        HwClock = 1 << 1,
        HwCompletion = 1 << 2,
        ZeroCopy = 1 << 3,
    };
    Q_DECLARE_FLAGS(kinds, kind)

    presentation(QObject* parent = nullptr);
    ~presentation() override;

    bool init_clock(bool clockid_valid, clockid_t clockid);

    void frame(render::wayland::output* output, std::deque<Toplevel*> const& windows);
    void lock(render::wayland::output* output, std::deque<Toplevel*> const& windows);
    void presented(render::wayland::output* output, uint32_t sec, uint32_t usec, kinds kinds);

private:
    uint32_t current_time() const;

    QHash<uint32_t, Wrapland::Server::Surface*> surfaces;

    clockid_t clockid;
    QElapsedTimer* fallback_clock{nullptr};
};

}
}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::render::wayland::presentation::kinds)
