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
#include "render/effects.h"
#include "render/gl/scene.h"
#include "render/qpainter/scene.h"
#include "wayland_logging.h"

#include <deque>
#include <map>
#include <memory>

namespace KWin::render::wayland
{

template<typename Platform>
class compositor
{
public:
    using platform_t = Platform;
    using type = compositor<Platform>;
    using scene_t = render::scene<Platform>;
    using effects_t = effects_handler_impl<type>;
    using space_t = typename Platform::base_t::space_t;
    using window_t = render::window<typename space_t::window_t, type>;

    compositor(Platform& platform)
        : qobject{std::make_unique<compositor_qobject>([this](auto /*te*/) { return false; })}
        , presentation{std::make_unique<wayland::presentation>(
              platform.base.get_clockid(),
              [&] { return platform.base.server->display->createPresentationManager(); })}
        , platform{platform}
        , dbus{std::make_unique<dbus::compositing<type>>(*this)}
    {
        compositor_setup(*this);

        dbus->qobject->integration.get_types = [] { return QStringList{"egl"}; };
    }

    ~compositor()
    {
        Q_EMIT this->qobject->aboutToDestroy();
        compositor_stop(*this, true);
        delete_unused_support_properties(*this);
        compositor_destroy_selection(*this);
    }

    void start(space_t& space)
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
                             [this] { full_repaint(*this); });
            QObject::connect(&this->platform.base,
                             &base::platform::output_removed,
                             this->qobject.get(),
                             [this](auto output) {
                                 for (auto& win : this->space->windows) {
                                     std::visit(overload{[&](auto&& win) {
                                                    remove_all(win->render_data.repaint_outputs,
                                                               output);
                                                }},
                                                win);
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

    void configChanged()
    {
        reinitialize_compositor(*this);
        full_repaint(*this);
    }

    template<typename Win>
    void schedule_repaint(Win* window)
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

    template<typename Win>
    void schedule_frame_callback(Win* window)
    {
        if (locked) {
            return;
        }

        if (auto max_out = max_coverage_output(window)) {
            max_out->render->request_frame(window);
        }
    }

    void toggleCompositing()
    {
        // For the shortcut. Not possible on Wayland because we always composite.
    }

    void addRepaint(QRegion const& region)
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

    std::unique_ptr<scene_t> create_scene()
    {
        if (this->platform.selected_compositor() == QPainterCompositing) {
            return qpainter::create_scene(this->platform);
        }
        return gl::create_scene(this->platform);
    }

    void performCompositing()
    {
        for (auto& output : this->platform.base.outputs) {
            output->render->run();
        }
    }

    std::unique_ptr<compositor_qobject> qobject;

    std::unique_ptr<scene_t> scene;
    std::unique_ptr<effects_t> effects;
    std::unique_ptr<wayland::presentation> presentation;
    std::unique_ptr<cursor<Platform>> software_cursor;

    render::state state{state::off};
    x11::compositor_selection_owner* m_selectionOwner{nullptr};

    QList<xcb_atom_t> unused_support_properties;
    QTimer unused_support_property_timer;

    Platform& platform;
    space_t* space{nullptr};

private:
    int locked{0};

    std::unique_ptr<dbus::compositing<type>> dbus;
};

}
