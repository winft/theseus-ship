/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"
#include "presentation.h"
#include "utils.h"

#include "main.h"
#include "render/compositor.h"
#include "render/compositor_start.h"
#include "render/cursor.h"
#include "render/dbus/compositing.h"
#include "render/gl/scene.h"
#include "render/qpainter/scene.h"
#include "wayland_logging.h"

#include <deque>
#include <map>
#include <memory>

namespace KWin::render::wayland
{

template<typename Platform>
class compositor : public render::compositor<Platform>
{
public:
    using platform_t = Platform;
    using type = compositor<Platform>;
    using abstract_type = render::compositor<Platform>;
    using scene_t = typename abstract_type::scene_t;
    using effects_t = effects_handler_impl<type>;
    using space_t = typename Platform::base_t::space_t;

    compositor(Platform& platform)
        : render::compositor<Platform>(platform)
        , presentation{std::make_unique<wayland::presentation>(platform.base.get_clockid())}
        , dbus{std::make_unique<dbus::compositing<type>>(*this)}
    {
        dbus->qobject->integration.get_types = [] { return QStringList{"egl"}; };

        QObject::connect(kwinApp(),
                         &Application::x11ConnectionAboutToBeDestroyed,
                         this->qobject.get(),
                         [this] { compositor_destroy_selection(*this); });
    }

    ~compositor() override
    {
        Q_EMIT this->qobject->aboutToDestroy();
        compositor_stop(*this, true);
        this->deleteUnusedSupportProperties();
        compositor_destroy_selection(*this);
    }

    void start(space_t& space) override
    {
        if (!this->space) {
            // On first start setup connections.
            QObject::connect(kwinApp(),
                             &Application::x11ConnectionChanged,
                             this->qobject.get(),
                             [this] { compositor_setup_x11_support(*this); });
            QObject::connect(space.stacking.order.qobject.get(),
                             &win::stacking_order_qobject::changed,
                             this->qobject.get(),
                             [this] { this->addRepaintFull(); });
            QObject::connect(&this->platform.base,
                             &base::platform::output_removed,
                             this->qobject.get(),
                             [this](auto output) {
                                 for (auto& win : this->space->windows) {
                                     remove_all(win->repaint_outputs, output);
                                 }
                             });
            QObject::connect(
                space.qobject.get(), &win::space_qobject::destroyed, this->qobject.get(), [this] {
                    for (auto& output : this->platform.base.outputs) {
                        output->render->delay_timer.stop();
                    }
                });
            this->space = &space;
        }

        // For now we use the software cursor as our wlroots backend does not support yet a hardware
        // cursor.
        using sw_cursor_t = typename decltype(this->software_cursor)::element_type;
        this->software_cursor = std::make_unique<sw_cursor_t>(this->platform);
        this->software_cursor->set_enabled(true);

        try {
            compositor_start_scene(*this);
        } catch (std::runtime_error const& ex) {
            qCCritical(KWIN_CORE) << "Error: " << ex.what();
            qCCritical(KWIN_CORE) << "Wayland requires compositing. Going to quit.";
            qApp->quit();
        }
    }

    void reinitialize()
    {
        reinitialize_compositor(*this);
    }

    void configChanged() override
    {
        reinitialize_compositor(*this);
        this->addRepaintFull();
    }

    void schedule_repaint(typename space_t::window_t* window) override
    {
        if (locked) {
            return;
        }

        for (auto& output : this->platform.base.outputs) {
            if (!win::visible_rect(window).intersected(output->geometry()).isEmpty()) {
                output->render->set_delay_timer();
            }
        }
    }

    void schedule_frame_callback(typename space_t::window_t* window) override
    {
        if (locked) {
            return;
        }

        if (auto max_out = max_coverage_output(window)) {
            max_out->render->request_frame(window);
        }
    }

    void toggleCompositing() override
    {
        // For the shortcut. Not possible on Wayland because we always composite.
    }

    void addRepaint(QRegion const& region) override
    {
        if (locked) {
            return;
        }
        for (auto& output : this->platform.base.outputs) {
            output->render->add_repaint(region);
        }
    }

    void check_idle()
    {
        for (auto& output : this->platform.base.outputs) {
            if (!output->render->idle) {
                return;
            }
        }
        this->scene->idle();
    }

    bool is_locked() const
    {
        return locked > 0;
    }

    void lock()
    {
        locked++;
    }

    void unlock()
    {
        assert(locked > 0);
        locked--;

        if (!locked) {
            this->addRepaintFull();
        }
    }

    std::unique_ptr<wayland::presentation> presentation;

    std::unique_ptr<scene_t> create_scene() override
    {
        if (this->platform.selected_compositor() == QPainterCompositing) {
            return qpainter::create_scene(this->platform);
        }
        return gl::create_scene(this->platform);
    }

    void performCompositing() override
    {
        for (auto& output : this->platform.base.outputs) {
            output->render->run();
        }
    }

    std::unique_ptr<effects_t> effects;
    std::unique_ptr<dbus::compositing<type>> dbus;

private:
    int locked{0};
};

}
