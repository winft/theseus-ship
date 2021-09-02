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

class KWIN_EXPORT compositor : public render::compositor
{
    Q_OBJECT
public:
    static compositor* create(QObject* parent = nullptr);

    void schedule_repaint(Toplevel* window) override;
    void schedule_frame_callback(Toplevel* window);

    void swapped(AbstractWaylandOutput* output);
    void swapped(AbstractWaylandOutput* output, unsigned int sec, unsigned int usec);

    void toggleCompositing() override;
    void addRepaint(QRegion const& region) override;
    void check_idle();

    render::wayland::presentation* presentation;
    std::map<AbstractWaylandOutput*, std::unique_ptr<render::wayland::output>> outputs;

protected:
    void start() override;
    std::deque<Toplevel*> performCompositing() override;

private:
    explicit compositor(QObject* parent);
    ~compositor();
};

}
}
