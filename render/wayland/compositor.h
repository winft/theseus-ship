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

class Toplevel;

namespace base::wayland
{
class output;
}

namespace render::wayland
{

class output;
class presentation;
struct presentation_data;

class KWIN_EXPORT compositor : public render::compositor
{
    Q_OBJECT
public:
    compositor(render::platform& platform);
    ~compositor();

    void start(win::space& space) override;

    void schedule_repaint(Toplevel* window) override;
    void schedule_frame_callback(Toplevel* window) override;

    void toggleCompositing() override;
    void addRepaint(QRegion const& region) override;
    void check_idle();

    bool is_locked() const;
    void lock();
    void unlock();

    std::unique_ptr<render::wayland::presentation> presentation;

protected:
    render::scene* create_scene() override;
    void performCompositing() override;

private:
    int locked{0};
};

}
}
