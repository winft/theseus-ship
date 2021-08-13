/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QBasicTimer>
#include <QRegion>
#include <QTimer>

#include <deque>
#include <map>

namespace Wrapland
{
namespace Server
{
class Surface;
}
}

namespace KWin
{
class AbstractWaylandOutput;
class Toplevel;

namespace render::wayland
{
class compositor;

class KWIN_EXPORT output : public QObject
{
    int index;
    wayland::compositor* compositor{nullptr};

    ulong msc{0};

    // Compositing delay (in ns).
    int64_t delay;
    int64_t last_paint_durations[2]{0};
    int paint_periods{0};

    QRegion repaints_region;

    bool prepare_run(QRegion& repaints, std::deque<Toplevel*>& windows);
    void retard_next_run();
    void swapped();

    void update_paint_periods(int64_t duration);
    int64_t refresh_length() const;

    void timerEvent(QTimerEvent* event) override;

public:
    AbstractWaylandOutput* base;
    std::map<uint32_t, Wrapland::Server::Surface*> assigned_surfaces;

    bool idle{true};
    bool swap_pending{false};
    QBasicTimer delay_timer;

    output(AbstractWaylandOutput* base, wayland::compositor* compositor);

    void add_repaint(QRegion const& region);
    void set_delay_timer();

    std::deque<Toplevel*> run();

    void swapped_sw();
    void swapped_hw(unsigned int sec, unsigned int usec);
};

}
}
