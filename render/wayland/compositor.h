/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/compositor.h"

#include <QRegion>

#include <deque>
#include <map>
#include <memory>

namespace KWin
{
class AbstractWaylandOutput;
class Toplevel;

namespace render::wayland
{
class output;
class presentation;
struct presentation_data;

class KWIN_EXPORT compositor : public render::compositor
{
    Q_OBJECT
public:
    compositor();
    ~compositor();

    void schedule_repaint(Toplevel* window) override;
    void schedule_frame_callback(Toplevel* window) override;

    void toggleCompositing() override;
    void addRepaint(QRegion const& region) override;
    void check_idle();

    bool is_locked() const;
    void lock();
    void unlock();

    render::wayland::presentation* presentation;
    std::map<AbstractWaylandOutput*, std::unique_ptr<render::wayland::output>> outputs;

protected:
    void start() override;
    std::deque<Toplevel*> performCompositing() override;

private:
    int locked{0};
};

}
}
