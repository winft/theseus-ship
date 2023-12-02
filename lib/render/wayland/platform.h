/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effects.h"

#include <render/backend/wlroots/backend.h>
#include <render/compositor_start.h>
#include <render/dbus/compositing.h>
#include <render/gl/backend.h>
#include <render/gl/egl_data.h>
#include <render/gl/scene.h>
#include <render/options.h>
#include <render/post/night_color_manager.h>
#include <render/qpainter/scene.h>
#include <render/singleton_interface.h>
#include <render/wayland/presentation.h>
#include <render/wayland/shadow.h>

#include <memory>

namespace KWin::render::wayland
{

template<typename Base>
class platform
{
public:
    using type = platform<Base>;
    using qobject_t = compositor_qobject;
    using base_t = Base;
    using space_t = typename Base::space_t;

    using backend_t = backend::wlroots::backend<type>;
    using state_t = render::state;
    using scene_t = render::scene<type>;
    using effects_t = wayland::effects_handler_impl<scene_t>;

    using window_t = render::window<typename space_t::window_t, type>;
    using buffer_t = buffer_win_integration<typename scene_t::buffer_t>;
    using shadow_t = render::shadow<window_t>;

    platform(Base& base)
        : base{base}
        , qobject{std::make_unique<compositor_qobject>([this](auto /*te*/) { return false; })}
        , options{std::make_unique<render::options>(base.operation_mode, base.config.main)}
        , backend{*this}
        , night_color{std::make_unique<render::post::night_color_manager<Base>>(base)}
        , presentation{std::make_unique<wayland::presentation>([&] {
            return std::make_unique<Wrapland::Server::PresentationManager>(
                base.server->display.get());
        })}
        , dbus{std::make_unique<dbus::compositing<type>>(*this)}
    {
        singleton_interface::get_egl_data = [this] { return egl_data; };

        compositor_setup(*this);

        dbus->qobject->integration.get_types = [] { return QStringList{"egl"}; };
    }

    virtual ~platform()
    {
        singleton_interface::get_egl_data = {};
    }

    bool requiresCompositing() const
    {
        return true;
    }

    bool compositingPossible() const
    {
        return true;
    }

    QString compositingNotPossibleReason() const
    {
        return {};
    }

    bool openGLCompositingIsBroken() const
    {
        return false;
    }

    void createOpenGLSafePoint(opengl_safe_point /*safePoint*/)
    {
    }

    render::outline_visual* create_non_composited_outline(render::outline* /*outline*/)
    {
        // Not possible on Wayland.
        return nullptr;
    }

    void invertScreen()
    {
        assert(effects);
        effects->invert_screen();
    }

    gl::backend<gl::scene<type>, type>* get_opengl_backend()
    {
        return backend.get_opengl_backend();
    }

    qpainter::backend<qpainter::scene<type>>* get_qpainter_backend()
    {
        return backend.get_qpainter_backend();
    }

    bool is_sw_compositing() const
    {
        return backend.is_sw_compositing();
    }

    // TODO(romangg): Remove the boolean trap.
    void render_stop(bool on_shutdown)
    {
        return backend.render_stop(on_shutdown);
    }

    void start(space_t& space)
    {
        if (!this->space) {
            // On first start setup connections.
            QObject::connect(space.stacking.order.qobject.get(),
                             &win::stacking_order_qobject::changed,
                             this->qobject.get(),
                             [this] { full_repaint(*this); });
            QObject::connect(space.qobject.get(),
                             &space_t::qobject_t::current_subspace_changed,
                             this->qobject.get(),
                             [this] { full_repaint(*this); });
            QObject::connect(
                &base, &base::platform::output_removed, this->qobject.get(), [this](auto output) {
                    for (auto& win : this->space->windows) {
                        std::visit(overload{[&](auto&& win) {
                                       remove_all(win->render_data.repaint_outputs, output);
                                   }},
                                   win);
                    }
                });
            QObject::connect(
                space.qobject.get(), &win::space_qobject::destroyed, this->qobject.get(), [this] {
                    for (auto& output : base.outputs) {
                        output->render->delay_timer.stop();
                    }
                });
            this->space = &space;
        }

        // For now we use the software cursor as our wlroots backend does not support yet a hardware
        // cursor.
        using sw_cursor_t = typename decltype(this->software_cursor)::element_type;
        this->software_cursor = std::make_unique<sw_cursor_t>(*this);
        this->software_cursor->set_enabled(true);

        try {
            if (compositor_prepare_scene(*this)) {
                compositor_start_scene(*this);
            }
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

        for (auto& output : base.outputs) {
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
        for (auto& output : base.outputs) {
            output->render->add_repaint(region);
        }
    }

    void check_idle()
    {
        for (auto& output : base.outputs) {
            if (!output->render->idle) {
                return;
            }
        }
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
        if (is_sw_compositing()) {
            return qpainter::create_scene(*this);
        }
        return gl::create_scene(*this);
    }

    template<typename RefWin>
    void integrate_shadow(RefWin& ref_win)
    {
        ref_win.render->shadow_windowing.create = create_shadow<shadow_t, RefWin>;
        ref_win.render->shadow_windowing.update = update_shadow<shadow_t, RefWin>;
    }

    void performCompositing()
    {
        for (auto& output : base.outputs) {
            output->render->run();
        }
    }

    Base& base;

    std::unique_ptr<compositor_qobject> qobject;
    gl::egl_data* egl_data{nullptr};

    state_t state{state::off};
    int output_index{0};

    std::unique_ptr<render::options> options;
    backend_t backend;
    std::unique_ptr<render::post::night_color_manager<Base>> night_color;

    std::unique_ptr<scene_t> scene;
    std::unique_ptr<effects_t> effects;
    std::unique_ptr<wayland::presentation> presentation;
    std::unique_ptr<cursor<type>> software_cursor;

    QList<xcb_atom_t> unused_support_properties;
    QTimer unused_support_property_timer;

    space_t* space{nullptr};

private:
    int locked{0};
    std::unique_ptr<dbus::compositing<type>> dbus;
};

}
