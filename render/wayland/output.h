/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "duration_record.h"
#include "render/gl/timer_query.h"

#include <kwin_export.h>

#include <QBasicTimer>
#include <QRegion>
#include <QTimer>

#include <chrono>
#include <deque>
#include <map>
#include <vector>

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
struct presentation_data;

class KWIN_EXPORT output : public QObject
{
    int index;
    wayland::compositor* compositor{nullptr};

    ulong msc{0};

    // Compositing delay.
    std::chrono::nanoseconds delay;

    duration_record paint_durations;
    duration_record render_durations;

    // Used for debugging rendering time.
    std::chrono::nanoseconds swap_ref_time;

    QRegion repaints_region;

    bool prepare_repaint(Toplevel* win);
    bool prepare_run(QRegion& repaints, std::deque<Toplevel*>& windows);
    void retard_next_run();

    std::chrono::nanoseconds refresh_length() const;

    void timerEvent(QTimerEvent* event) override;

public:
    AbstractWaylandOutput* base;
    std::map<uint32_t, Wrapland::Server::Surface*> assigned_surfaces;

    bool idle{true};
    bool swap_pending{false};
    QBasicTimer delay_timer;
    QBasicTimer frame_timer;
    std::vector<render::gl::timer_query> last_timer_queries;

    output(AbstractWaylandOutput* base, wayland::compositor* compositor);

    void add_repaint(QRegion const& region);
    void set_delay(presentation_data const& data);
    void set_delay_timer();
    void request_frame(Toplevel* window);

    std::deque<Toplevel*> run();
    void dry_run();

    void swapped(presentation_data const& data);
};

}
}
