/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "app.h"

#include "config-kwin.h"

#include "base/seat/backend/wlroots/session.h"
#include "base/wayland/output.h"
#include "debug/console/wayland/wayland_console.h"
#include "desktop/screen_locker_watcher.h"
#include "input/backend/wlroots/platform.h"
#include "input/wayland/cursor.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "render/backend/wlroots/output.h"
#include "render/effects.h"
#include "render/wayland/compositor.h"
#include "scripting/platform.h"
#include "win/screen.h"
#include "win/shortcuts_init.h"
#include "win/wayland/space.h"
#include "xwl/xwayland.h"

#include <KCrash>
#include <KPluginMetaData>
#include <QAbstractEventDispatcher>
#include <QPluginLoader>
#include <QSocketNotifier>
#include <QThread>
#include <QtConcurrentRun>
#include <Wrapland/Server/display.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include <wlr/backend/headless.h>
#if HAVE_WLR_BASE_INPUT_DEVICES
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
#endif
}

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KGlobalAccelImpl)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

namespace KWin
{

void disable_dr_konqi()
{
    KCrash::setDrKonqiEnabled(false);
}
Q_CONSTRUCTOR_FUNCTION(disable_dr_konqi)

WaylandTestApplication::WaylandTestApplication(OperationMode mode,
                                               std::string const& socket_name,
                                               base::wayland::start_options flags,
                                               int& argc,
                                               char** argv)
    : Application(mode, argc, argv)
{
    // TODO: add a test move to kglobalaccel instead?
    QFile{QStandardPaths::locate(QStandardPaths::ConfigLocation,
                                 QStringLiteral("kglobalshortcutsrc"))}
        .remove();

    QIcon::setThemeName(QStringLiteral("breeze"));

    qunsetenv("XKB_DEFAULT_RULES");
    qunsetenv("XKB_DEFAULT_MODEL");
    qunsetenv("XKB_DEFAULT_LAYOUT");
    qunsetenv("XKB_DEFAULT_VARIANT");
    qunsetenv("XKB_DEFAULT_OPTIONS");

    const auto ownPath = libraryPaths().constLast();
    removeLibraryPath(ownPath);
    addLibraryPath(ownPath);

    server.reset(new base::wayland::server(socket_name, flags));
    base = base::backend::wlroots::platform(server->display.get(),
                                            wlr_headless_backend_create(server->display->native()));

    base.render = std::make_unique<render::backend::wlroots::platform<decltype(base)>>(base);

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), socket_name.c_str());
    setProcessStartupEnvironment(environment);
}

WaylandTestApplication::~WaylandTestApplication()
{
#if HAVE_WLR_BASE_INPUT_DEVICES
    assert(keyboard);
    assert(pointer);
    assert(touch);
    wlr_keyboard_finish(keyboard);
    wlr_pointer_finish(pointer);
    wlr_touch_finish(touch);
#endif

    setTerminating();

    // need to unload all effects prior to destroying X connection as they might do X calls
    // also before destroy Workspace, as effects might call into Workspace
    if (effects) {
        static_cast<render::effects_handler_impl*>(effects)->unloadAllEffects();
    }

    // Kill Xwayland before terminating its connection.
    base.xwayland.reset();
    base.xwayland_interface = nullptr;
    waylandServer()->terminateClientConnections();

    // Block compositor to prevent further compositing from crashing with a null workspace.
    // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
    static_cast<render::wayland::compositor*>(base.render->compositor.get())->lock();

    base.space.reset();
    base.render->compositor.reset();
}

bool WaylandTestApplication::is_screen_locked() const
{
    if (!server) {
        return false;
    }
    return server->is_screen_locked();
}

base::platform& WaylandTestApplication::get_base()
{
    return base;
}

base::wayland::server* WaylandTestApplication::get_wayland_server()
{
    return server.get();
}

debug::console* WaylandTestApplication::create_debug_console()
{
    return new debug::wayland_console(*base.space);
}

void WaylandTestApplication::start()
{
    prepare_start();

    auto headless_backend = base::backend::wlroots::get_headless_backend(base.backend);
    wlr_headless_add_output(headless_backend, 1280, 1024);

    createOptions();

    session = std::make_unique<base::seat::backend::wlroots::session>(headless_backend);
    base.input = std::make_unique<input::backend::wlroots::platform>(base);
    base.input->install_shortcuts();

#if HAVE_WLR_BASE_INPUT_DEVICES
    keyboard = static_cast<wlr_keyboard*>(calloc(1, sizeof(wlr_keyboard)));
    pointer = static_cast<wlr_pointer*>(calloc(1, sizeof(wlr_pointer)));
    touch = static_cast<wlr_touch*>(calloc(1, sizeof(wlr_touch)));
#else
    keyboard = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_KEYBOARD);
    pointer = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_POINTER);
    touch = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_TOUCH);
#endif
    assert(keyboard);
    assert(pointer);
    assert(touch);

    try {
        static_cast<render::backend::wlroots::platform<decltype(base)>*>(base.render.get())->init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        ::exit(1);
    }

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_keyboard_init(keyboard, nullptr, "headless-keyboard");
    wlr_pointer_init(pointer, nullptr, "headless-pointer");
    wlr_touch_init(touch, nullptr, "headless-touch");

    Test::wlr_signal_emit_safe(&base.backend->events.new_input, keyboard);
    Test::wlr_signal_emit_safe(&base.backend->events.new_input, pointer);
    Test::wlr_signal_emit_safe(&base.backend->events.new_input, touch);
#endif

    // Must set physical size for calculation of screen edges corner offset.
    // TODO(romangg): Make the corner offset calculation not depend on that.
    auto out = base.outputs.at(0);
    out->wrapland_output()->set_physical_size(QSize(1280, 1024));

    try {
        base.render->compositor = std::make_unique<render::wayland::compositor>(*base.render);
    } catch (std::system_error const& exc) {
        std::cerr << "FATAL ERROR: compositor creation failed: " << exc.what() << std::endl;
        exit(exc.code().value());
    }

    base.space = std::make_unique<wayland_space>(base, server.get());
    input::wayland::add_dbus(base.input.get());
    win::init_shortcuts(*base.space);
    base.space->scripting = std::make_unique<scripting::platform>(*base.space);

    base.render->compositor->start(*base.space);

    waylandServer()->create_addons([this] { handle_server_addons_created(); });
    kwinApp()->screen_locker_watcher->initialize();
}

void WaylandTestApplication::set_outputs(size_t count)
{
    auto outputs = std::vector<Test::output>();
    auto const size = QSize(1280, 1024);
    auto width = 0;

    for (size_t i = 0; i < count; i++) {
        outputs.push_back({{QPoint(width, 0), size}});
        width += size.width();
    }

    set_outputs(outputs);
}

void WaylandTestApplication::set_outputs(std::vector<QRect> const& geometries)
{
    auto outputs = std::vector<Test::output>();
    for (auto&& geo : geometries) {
        outputs.push_back(geo);
    }
    set_outputs(outputs);
}

void WaylandTestApplication::set_outputs(std::vector<Test::output> const& outputs)
{
    auto outputs_copy = base.all_outputs;
    for (auto output : outputs_copy) {
        delete output;
    }

    for (auto&& output : outputs) {
        auto const size = output.geometry.size() * output.scale;

        wlr_headless_add_output(base.backend, size.width(), size.height());
        base.all_outputs.back()->force_geometry(output.geometry);
    }

    base::update_output_topology(base);
}

void WaylandTestApplication::handle_server_addons_created()
{
    if (operationMode() == OperationModeXwayland) {
        create_xwayland();
        return;
    }

    Q_EMIT startup_finished();
}

void WaylandTestApplication::create_xwayland()
{
    auto status_callback = [this](auto error) {
        if (error) {
            // we currently exit on Xwayland errors always directly
            // TODO: restart Xwayland
            std::cerr << "Xwayland had a critical error. Going to exit now." << std::endl;
            exit(error);
        }
        Q_EMIT startup_finished();
    };

    try {
        base.xwayland
            = std::make_unique<xwl::xwayland<wayland_space>>(this, *base.space, status_callback);
        base.xwayland_interface = base.xwayland.get();
    } catch (std::system_error const& exc) {
        std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
        exit(exc.code().value());
    } catch (std::exception const& exc) {
        std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
        exit(1);
    }
}

}
